import struct, zlib, pathlib, sys
from xml.dom import minidom, Node

def esc_text(t):
    return t.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
def esc_attr(t):
    return esc_text(t).replace('"',"&quot;")

def save_node(node, depth, out):
    t = node.nodeType
    if t == Node.ELEMENT_NODE:
        out.append("<"+node.tagName)
        attrs = node.attributes
        for i in range(attrs.length):
            a = attrs.item(i)
            out.append(' %s="%s"' % (a.name, esc_attr(a.value)))
        kids = [c for c in node.childNodes]
        if not kids:
            out.append("/>")
            return
        out.append(">")
        for c in kids:
            if c.nodeType in (Node.TEXT_NODE,):
                out.append(esc_text(c.data))
            elif c.nodeType == Node.CDATA_SECTION_NODE:
                out.append("<![CDATA["+c.data+"]]>")
            elif c.nodeType == Node.COMMENT_NODE:
                out.append("<!--"+c.data+"-->")
            elif c.nodeType == Node.ELEMENT_NODE:
                out.append("\n" + "  "*(depth+1))
                save_node(c, depth+1, out)
            else:
                out.append("")
        if kids[-1].nodeType == Node.ELEMENT_NODE:
            out.append("\n" + "  "*depth)
        out.append("</"+node.tagName+">")
    elif t == Node.DOCUMENT_NODE:
        for c in node.childNodes:
            save_node(c, depth, out)
    elif t == Node.DOCUMENT_TYPE_NODE:
        out.append("<!DOCTYPE "+node.name+">\n")
    elif t == Node.PROCESSING_INSTRUCTION_NODE:
        out.append("<?%s %s?>\n" % (node.target, node.data))
    elif t == Node.TEXT_NODE:
        out.append(esc_text(node.data))

def serialize(xml_bytes):
    doc = minidom.parseString(xml_bytes)
    out = ['<?xml version="1.0"?>\n']
    save_node(doc, 0, out)
    return "".join(out).encode()

def load(f):
    b = pathlib.Path(f).read_bytes()
    if f.endswith(("mmpz","xptz")):
        return zlib.decompress(b[4:])
    return b

for f in sys.argv[1:]:
    x = load(f)
    y = serialize(x)
    print(("MATCH " if x==y else "DIFF  ")+f, len(x), len(y))
    if x != y:
        # show first difference
        for i,(a,b) in enumerate(zip(x,y)):
            if a!=b:
                print("  first diff at", i, repr(x[max(0,i-60):i+60]), "||", repr(y[max(0,i-60):i+60])); break
