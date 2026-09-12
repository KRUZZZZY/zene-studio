import sys
from PySide6.QtGui import QImage, QPainter, QColor
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtCore import Qt
svgs = {"plugin":("data/themes/default/zene-plugin-logo.svg",16),
        "app":("cmake/linux/icons/scalable/apps/zene.svg",16),
        "mime":("cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg",16),
        "app64":("cmake/linux/icons/scalable/apps/zene.svg",64),
        "plugin64":("data/themes/default/zene-plugin-logo.svg",64)}
for name,(p,px) in svgs.items():
    r=QSvgRenderer(p)
    if not r.isValid(): print(name,"RENDERER INVALID"); continue
    img=QImage(px,px,QImage.Format_ARGB32); img.fill(QColor(0,0,0,0))
    pt=QPainter(img); r.render(pt); pt.end()
    img.save(f"evidence/render/{name}-{px}.png")
    # alpha/brightness map over a mid-grey backdrop so both light and dark ink show
    print(f"--- {name} {px}px (bg #808080; '.'=bg, '#'=light ink, 'o'=dark ink) ---")
    for y in range(px):
        row=''
        for x in range(px):
            c=img.pixelColor(x,y); a=c.alpha()
            if a<40: row+='.'
            else:
                lum=(c.red()*299+c.green()*587+c.blue()*114)//1000
                row += '#' if lum>140 else 'o'
        print(row)
