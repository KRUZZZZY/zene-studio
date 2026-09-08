#include <QtCore>
#include <QtXml/QDomDocument>
#include <cstdio>
int main(int argc, char** argv) {
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    QByteArray raw = f.readAll();
    QByteArray xml = QString(argv[1]).endsWith(".mmpz") ? qUncompress(raw) : raw;
    QDomDocument doc; doc.setContent(xml);
    QString out; QTextStream ts(&out); doc.save(ts, 2);
    QFile o(argv[2]); o.open(QIODevice::WriteOnly); o.write(out.toUtf8());
    return 0;
}
