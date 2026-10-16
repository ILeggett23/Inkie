from pathlib import Path
from reportlab.pdfgen import canvas
r=Path(__file__).resolve().parents[1]/'tests/fixtures';r.mkdir(exist_ok=True)
c=canvas.Canvas(str(r/'text-semantics.pdf'),pagesize=(600,800),invariant=1,pageCompression=0)
t=c.beginText(40,730);t.setFont('Helvetica',18);t.setLeading(25)
for line in ["A reader reads pages.","Readers can't read a blank page.","Cafe, café! Unicode and punctuation."]:t.textLine(line)
c.drawText(t);c.save()
c=canvas.Canvas(str(r/'no-text.pdf'),pagesize=(600,800),invariant=1,pageCompression=0)
c.rect(40,100,200,300,fill=1,stroke=1);c.save()
print('GENERATED_TEST_ONLY_PDFS',r)
