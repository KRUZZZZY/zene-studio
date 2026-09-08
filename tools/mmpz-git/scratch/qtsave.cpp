// Reference: what does Qt's QDomDocument::save(indent=2) produce for a real project?
#include <QtCore>
#include <QtXml/QDomDocument>
#include <cstdio>
int main(int argc, char** argv) {
    QFile f(argv[1]);
    f.open(QIODevice::ReadOnly);
    QByteArray raw = f.readAll();
    QByteArray xml = raw;
    if (QString(argv[1]).endsWith(".mmpz") || QString(argv[1]).endsWith(".xptz"))
        xml = qUncompress(raw);
    QDomDocument doc;
    QString err; int line=0, col=0;
    if (!doc.setContent(xml, &err, &line, &col)) { qWarning("parse fail %s at %d:%d", qPrintable(err), line, col); return 2; }
    QString out; QTextStream ts(&out);
    doc.save(ts, 2);
    QByteArray res = out.toUtf8();
    printf("orig=%d resaved=%d identical=%s\n", xml.size(), res.size(), (res==xml)?"YES":"NO");
    if (res != xml) {
        for (int i=0;i<qMin(res.size(),xml.size());++i) if (res[i]!=xml[i]) {
            printf("first diff at %d\n  ORIG: %s\n  SAVE: %s\n", i,
              xml.mid(qMax(0,i-50),110).constData(), res.mid(qMax(0,i-50),110).constData()); break; }
    }
    return 0;
}
