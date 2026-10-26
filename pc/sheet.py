import sys
from PIL import Image
out=sys.argv[1]; files=sys.argv[2:]; cols=3
ims=[Image.open(f).convert('RGB') for f in files]
w,h=ims[0].size; rows=(len(ims)+cols-1)//cols
s=Image.new('RGB',(w*cols,h*rows))
for i,im in enumerate(ims): s.paste(im.resize((w,h)),((i%cols)*w,(i//cols)*h))
s=s.resize((s.width*2//3, s.height*2//3)); s.save(out)
