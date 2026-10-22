import re, json, sys
path='/sdcard/Download/james_dump/dump.cs'
ns=''; dll=''
classes=[]
cur=None
# regex
re_dll=re.compile(r'^// Dll : (.+)$')
re_ns=re.compile(r'^// Namespace: (.*)$')
re_cls=re.compile(r'^(?:\[[^\]]*\]\s*)*(?:public|internal|private|protected|sealed|abstract|static|readonly|partial|\s)*\s*(class|struct|enum|interface)\s+([^\s:]+)')
re_field=re.compile(r'^\s*(?:public|private|protected|internal|readonly|static|const|volatile|\s)+([^;]+?)\s+([A-Za-z_<>0-9\.\[\]]+);\s*//\s*(0x[0-9A-Fa-f]+)')
re_meth=re.compile(r'^\s*(?:public|private|protected|internal|static|virtual|override|sealed|abstract|new|\s)+([^\{;]+?)\s*\(([^)]*)\)\s*\{\s*\}\s*$')
with open(path, encoding='utf-8', errors='ignore') as f:
    for line in f:
        m=re_dll.match(line)
        if m: dll=m.group(1); continue
        m=re_ns.match(line)
        if m: ns=m.group(1); continue
        m=re_cls.match(line)
        if m and m.group(1) in ('class','struct','enum','interface'):
            cur={'dll':dll,'ns':ns,'kind':m.group(1),'name':m.group(2),'fields':[],'methods':[],'line':0}
            classes.append(cur); continue
        if cur is not None:
            m=re_field.match(line)
            if m:
                cur['fields'].append((m.group(2).strip(),m.group(1).strip(),m.group(3)))
                continue
            m=re_meth.match(line)
            if m and ('RVA:' not in line):
                cur['methods'].append((m.group(1).strip(),m.group(2).strip()))
                continue
            if line.startswith('}') and cur:
                cur=None
# save json
out='/data/data/com.termux/files/usr/tmp/opencode/dump_index.json'
with open(out,'w') as g: json.dump(classes,g)
print('classes',len(classes),'->',out)
