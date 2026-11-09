#!/usr/bin/env python3
"""Reproduce FUN_10003f00's embedded catalog from the read-only extracted ASI."""
import argparse, hashlib, json, pathlib, struct
ROOT=pathlib.Path(__file__).resolve().parents[1]
def generate():
    data=(ROOT.parent/"ZolikaPatch_extracted/ZolikaPatch.asi").read_bytes()
    if hashlib.sha256(data).hexdigest()!="720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5":
        raise ValueError("unrecognized evidence binary")
    pe=struct.unpack_from("<I",data,60)[0];opt=pe+24
    image_base=struct.unpack_from("<I",data,opt+28)[0]
    section_start=opt+struct.unpack_from("<H",data,pe+20)[0]
    sections=[struct.unpack_from("<8sIIIIIIHHI",data,section_start+40*i) for i in range(struct.unpack_from("<H",data,pe+6)[0])]
    def offset(va,size=1):
        rva=va-image_base
        for section in sections:
            if section[2]<=rva and rva+size<=section[2]+section[3]:return section[4]+rva-section[2]
        raise ValueError("unmapped catalog pointer")
    table=offset(0x100de770,0x9637*4);names=[]
    for i in range(0x9637):
        start=offset(struct.unpack_from("<I",data,table+4*i)[0]);end=data.find(b"\0",start,start+1024)
        if end<start:raise ValueError("unterminated model name")
        names.append(data[start:end].decode("ascii"))
    return "// Generated from ZolikaPatch.asi table 0x100de770; original order preserved.\n"+"".join("  "+json.dumps(name)+",\n" for name in names)
if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--check",action="store_true");args=parser.parse_args()
    output=ROOT/"src/model_catalog.inc";text=generate()
    if args.check:
        if output.read_text()!=text:raise SystemExit("model catalog is stale")
        print("model catalog verified: 38455 names")
    else:output.write_text(text)
