import re, html, sys
t = open(sys.argv[1], encoding="utf-8").read()
t = html.unescape(re.search(r"<pre[^>]*>(.*?)</pre>", t, re.S).group(1))
t = t.replace("ſ", "s")
t = re.sub(r"[¬-]\s*\n\s*", "", t).lower()
t = re.sub(r"[^a-zäöüßéèêàâçîôûëïñ.,;:!?'\" \n-]", " ", t)
t = re.sub(r"\s+", " ", t)
open(sys.argv[2], "w", encoding="utf-8").write(t)
