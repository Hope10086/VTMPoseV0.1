import os

Sequence = []
key = 'Total Frames'
encTime = 'Total Time'

summary = []

path = os.getcwd()
for s in os.listdir():
    if os.path.isdir(s):
        Sequence.append(s)

for sequence in Sequence:
    os.chdir(os.path.join(path, sequence))
    files = os.listdir()
    for file in files:
        if file.split('.')[1] == 'txt' and "enc" in file:
            with open(file, 'r') as f:
                lines = f.readlines()
                PsnrIndex = 0
                TimeIndex = 0
                for line in lines:
                    if key in line:
                        PsnrIndex = lines.index(line) + 1
                    if encTime in line:
                        TimeIndex = lines.index(line)

                if PsnrIndex:
                    line = lines[PsnrIndex]
                    bd = line.split()[2]
                    ypsnr = line.split()[3]
                    upsnr = line.split()[4]
                    vpsnr = line.split()[5]
                    stream = bd + '\t' + ypsnr + '\t' + upsnr + '\t' + vpsnr + '\t'
                if TimeIndex:
                    line = lines[TimeIndex]
                    stream += line.split()[2]
                    stream += '\n'
                summary.append(stream)
    os.chdir('..')

with open('summary.txt', 'w') as f:
    for i in range(len(summary)):
        for item in summary[i]:
            f.write(item)