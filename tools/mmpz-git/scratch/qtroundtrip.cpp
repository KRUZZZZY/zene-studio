// Cross-check: Qt's qCompress/qUncompress vs our byte-level understanding.
#include <QtCore>
#include <cstdio>
int main(int argc, char** argv) {
    QFile f(argv[1]);
    if (!f.open(QIODevice::ReadOnly)) { qWarning("open fail"); return 2; }
    QByteArray raw = f.readAll();
    QByteArray un = qUncompress(raw);
    if (un.isEmpty()) { qWarning("qUncompress failed"); return 3; }
    QByteArray re = qCompress(un);
    printf("orig=%d un=%d re=%d identical=%s\n", raw.size(), un.size(), re.size(),
           (re == raw) ? "YES" : "NO");
    QFile o(argv[2]);
    o.open(QIODevice::WriteOnly);
    o.write(re);
    return 0;
}
