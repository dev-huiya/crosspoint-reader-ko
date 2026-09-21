from pathlib import Path
import sys
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

out = Path(sys.argv[1])
chapter = Path(__file__).with_name("korean.xhtml").read_bytes()
container = b'''<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>'''
package = '''<?xml version="1.0" encoding="UTF-8"?>
<package version="3.0" unique-identifier="id" xmlns="http://www.idpf.org/2007/opf">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">ko-desktop-smoke</dc:identifier><dc:title>한글 테스트</dc:title><dc:language>ko</dc:language></metadata>
  <manifest><item id="chapter1" href="chapter1.xhtml" media-type="application/xhtml+xml"/><item id="chapter2" href="chapter2.xhtml" media-type="application/xhtml+xml"/></manifest>
  <spine><itemref idref="chapter1"/><itemref idref="chapter2"/></spine>
</package>'''.encode("utf-8")

out.parent.mkdir(parents=True, exist_ok=True)
with ZipFile(out, "w") as archive:
    archive.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
    archive.writestr("META-INF/container.xml", container, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/content.opf", package, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/chapter1.xhtml", chapter, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/chapter2.xhtml", chapter, compress_type=ZIP_DEFLATED)
