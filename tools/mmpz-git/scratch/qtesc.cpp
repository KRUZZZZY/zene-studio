#include <QtCore>
#include <QtXml/QDomDocument>
#include <cstdio>
int main() {
    QDomDocument d;
    QDomElement root = d.createElement("r");
    d.appendChild(root);
    // text node with every interesting char
    QString t = QString::fromUtf8("A&B<C>D\"E'F\rG\nH\tI");
    root.appendChild(d.createTextNode(t));
    QDomElement e = d.createElement("e");
    e.setAttribute("a", t);
    root.appendChild(e);
    QString out; QTextStream ts(&out); d.save(ts, 2);
    printf("---\n%s\n---\n", out.toUtf8().constData());
    printf("bytes: ");
    QByteArray b = out.toUtf8();
    for (int i=0;i<b.size();++i) printf("%02x ", (unsigned char)b[i]);
    printf("\n");
    return 0;
}
