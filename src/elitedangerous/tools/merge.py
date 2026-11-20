# Merge translations: text/ja/all.json (chunk output) + review fixes + manual fixes -> text/ja/merged.json
#   review fixes that drop English words (ship / brand names, kept English by rule) are rejected
import json, glob, re, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
PH = re.compile(r"\$[^;\s$]{1,200};|%[-+0#]*\d*(?:\.\d+)?(?:ll|l|h|I64)?[sdiufxXcpgeE]|<[^<>]{1,200}>|&[a-z]+;")
J = lambda p: json.load(open(os.path.join(ROOT, p), encoding='utf-8'))
d = J('text/ja/all.json')
words = lambda s: set(w for w in re.findall(r'[A-Za-z][A-Za-z\-]{2,}', PH.sub(' ', s)))
rej = 0
for f in sorted(glob.glob(os.path.join(ROOT, 'text', 'review_out', '*.json'))):
    for e, j in json.load(open(f, encoding='utf-8')).items():
        if e not in d or not isinstance(j, str): continue
        if sorted(PH.findall(e)) != sorted(PH.findall(j)) or e.count('\n') != j.count('\n'): continue
        if not words(d[e]) <= words(j): rej += 1; continue
        d[e] = j
fx = J('text/ja/zz_fix.json')
d.update(fx)
# consistency
for e in list(d):
    j = d[e]
    if e.endswith('Visitor Beacon'):
        j = re.sub(r'(ビジター・ビーコン|訪問者ビーコン|来訪者ビーコン|訪問ビーコン|Visitor Beacon)$', 'ビジタービーコン', j)
    d[e] = j
json.dump(d, open(os.path.join(ROOT, 'text', 'ja', 'merged.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('merged', len(d), 'review rejected', rej)
