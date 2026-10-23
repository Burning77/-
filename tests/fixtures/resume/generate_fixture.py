"""Generate a tiny synthetic OOXML resume; contains no user data."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
with ZipFile(Path(__file__).with_name("candidate.docx"), "w", ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", '''<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/></Types>''')
    z.writestr("_rels/.rels", f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="{R}/officeDocument" Target="word/document.xml"/></Relationships>')
    z.writestr("word/_rels/document.xml.rels", f'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="{R}/header" Target="header1.xml"/></Relationships>')
    z.writestr("word/header1.xml", f'<w:hdr xmlns:w="{W}"><w:p><w:r><w:t>候选人：张三</w:t></w:r></w:p></w:hdr>')
    z.writestr("word/document.xml", f'''<w:document xmlns:w="{W}" xmlns:r="{R}"><w:body><w:p><w:r><w:t>技能：C++、Qt、FreeRTOS</w:t></w:r></w:p><w:tbl><w:tr><w:tc><w:p><w:r><w:t>雷达信号处理</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>C6678 DSP 项目</w:t></w:r></w:p></w:tc></w:tr></w:tbl><w:sectPr><w:headerReference w:type="default" r:id="rId1"/></w:sectPr></w:body></w:document>''')
