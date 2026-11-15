#!/usr/bin/env python3
"""
Low-level MPC skin builder for the plugin's graphic panel.

Emits the custom panel skin:
  - full-screen page background PNGs (dark panel, cyan/teal fluorescent
    graphics) with baked section panels and
    titles. Envelope curves are NOT baked in: each envelope is the MPC's live,
    touch-editable Envelope widget (see env_widget).
  - TUI.json  : tabs + custom components + control entries bound to the exact
                frozen parameter names/indices. Overlays keep the mandatory
                duplicate "version" keys (written via a pairs-aware serializer).
  - Q-Links.json : per-tab hardware knob mapping.

Nothing here touches the DSP or parameter set; it only reads params.json for
names/indices and default values.
"""
import json, os, sys, shutil
from PIL import Image, ImageDraw, ImageFont, ImageFilter

# Paths are relative to the plugin project (the folder holding plugin.json): this script sits
# either in that folder or in its tools/ subfolder. Override with SKIN_PROJECT / SKIN_PARAMS /
# SKIN_KIT. params.json comes from an SDK build (tools/build.py writes build/params.json); the
# kit is the SDK's cyan film-strip set (python tools/make_ui_kit.py --theme cyan --out <dir>).
_HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.environ.get("SKIN_PROJECT") or (_HERE if os.path.isfile(os.path.join(_HERE, "plugin.json")) else os.path.dirname(_HERE))
PARAMS = json.load(open(os.environ.get("SKIN_PARAMS") or os.path.join(PROJECT, "build", "params.json")))
PLIST = PARAMS["params"]
NAME2IDX = {p["name"]: p["index"] for p in PLIST}
IDX2P = {p["index"]: p for p in PLIST}

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sq80skin"
# Panel wordmark and tagline. The public ESQUE80 build overrides both (see make_public.py).
BRAND = os.environ.get("SKIN_BRAND", "SQ-80")
TAGLINE = os.environ.get("SKIN_TAGLINE", "ENSONIQ-STYLE CROSS-WAVE SYNTHESIS")
KIT = os.environ.get("SKIN_KIT") or os.path.join(PROJECT, "build", "ui-kit-cyan-ff19c6e0")
os.makedirs(OUT, exist_ok=True)

# ---- palette (cyan-on-dark SQ-80 aesthetic) ----
BG       = (16, 22, 25)      # ff10161a  deep teal-black
PANEL    = (23, 31, 35)      # panel fill
PANEL_HI = (30, 41, 46)      # panel top highlight
BORDER   = (38, 76, 84)      # ff264c54 dim teal border
BORDER_HI= (60, 120, 132)
CYAN     = (25, 198, 224)    # ff19c6e0 accent
CYAN_DK  = (21, 134, 151)    # ff158697
TEXT     = (230, 247, 251)   # ffe6f7fb
MUTED    = (92, 112, 120)    # ff5c7078
GRID     = (30, 54, 60)

W, H = 1280, 628

# ---- fonts ----
def _font(paths, size):
    for p in paths:
        if os.path.exists(p):
            try: return ImageFont.truetype(p, size)
            except Exception: pass
    return ImageFont.load_default()

SANS = ["/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf"]
SANS_B = ["/System/Library/Fonts/Supplemental/Arial Bold.ttf",
          "/System/Library/Fonts/Helvetica.ttc",
          "/Library/Fonts/Arial Bold.ttf"]
MONO = ["/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Supplemental/Courier New.ttf"]

def font(size, bold=True):
    return _font(SANS_B if bold else SANS, size)

# ---------------------------------------------------------------- drawing utils
def panel(d, x, y, w, h, title=None, accent=CYAN):
    r = 14
    d.rounded_rectangle([x, y, x+w, y+h], radius=r, fill=PANEL,
                        outline=BORDER, width=2)
    # top highlight strip
    d.rounded_rectangle([x+2, y+2, x+w-2, y+34], radius=r-3, fill=PANEL_HI)
    d.rectangle([x+2, y+22, x+w-2, y+34], fill=PANEL_HI)
    if title:
        f = font(23, True)
        # accent tick
        d.rectangle([x+16, y+11, x+22, y+27], fill=accent)
        glow_text(d, x+34, y+7, title, f, accent, spacing=2)

def glow_text(d, x, y, text, f, color, spacing=0, anchor=None):
    """Fluorescent text: soft glow halo + crisp core."""
    img = d._image
    layer = Image.new("RGBA", img.size, (0,0,0,0))
    ld = ImageDraw.Draw(layer)
    try:
        ld.text((x, y), text, font=f, fill=color+(255,), anchor=anchor)
    except TypeError:
        ld.text((x, y), text, font=f, fill=color+(255,))
    glow = layer.filter(ImageFilter.GaussianBlur(4))
    img.alpha_composite(glow)
    img.alpha_composite(glow)   # double for brightness
    img.alpha_composite(layer)

def ctext(d, cx, y, text, f, color, glow=False):
    w = d.textlength(text, font=f)
    if glow: glow_text(d, cx-w/2, y, text, f, color)
    else: d.text((cx-w/2, y), text, font=f, fill=color)

def wave_glyph(d, x, y, w, h, kind):
    """little cyan waveform glyph (saw/square/tri) for LFO panels."""
    mid=y+h/2; pts=[]
    n=2
    if kind=="saw":
        for i in range(n+1):
            pts += [(x+w*i/n, y+h), (x+w*i/n, y)]
    elif kind=="sqr":
        step=w/(2*n)
        yy=y
        cx=x
        pts=[(x,mid)]
        for i in range(2*n):
            pts.append((cx, y if i%2==0 else y+h)); cx+=step; pts.append((cx, y if i%2==0 else y+h))
    else:  # tri
        for i in range(2*n+1):
            pts.append((x+w*i/(2*n), y if i%2 else y+h))
    d.line(pts, fill=CYAN_DK, width=2, joint="curve")

# =============================================================== PAGE BACKGROUNDS
def new_page():
    img = Image.new("RGBA", (W, H), BG+(255,))
    d = ImageDraw.Draw(img)
    d._image = img
    # faint vignette grid texture
    return img, d

def save(img, name):
    img.convert("RGB").save(os.path.join(OUT, name))

# page 1 : OSC
def page_osc():
    img,d=new_page()
    px=[16,440,864]; pw=400
    for i,X in enumerate(px):
        panel(d, X, 16, pw, 596, "OSC %d"%(i+1))
        # section sublabels baked under each control cluster
        ctext(d, X+200, 160, "OSC %d PITCH"%(i+1), font(14,False), MUTED)   # clear of the wave control's focus box
        ctext(d, X+147, 316, "DCA %d"%(i+1), font(14,False), MUTED)
        wave_glyph(d, X+300, 70, 70, 30, ["saw","saw","sqr"][i])
    save(img,"bg_osc.png")

# OSC key sub-pages: pitch-mod routes and DCA (level) mod routes, one panel per oscillator
def page_routes(fname, title_fmt):
    img,d=new_page()
    px=[16,440,864]; pw=400
    for i,X in enumerate(px):
        panel(d, X, 16, pw, 596, title_fmt%(i+1))
        ctext(d, X+200, 54,  "MOD 1 SOURCE", font(14,False), MUTED)
        d.line([(X+24, 330), (X+pw-24, 330)], fill=BORDER, width=1)
        ctext(d, X+200, 336, "MOD 2 SOURCE", font(14,False), MUTED)
    save(img,fname)

# LFO key sub-page: depth-mod source grid, final level, reset / humanize, one panel per LFO
def page_lfomod():
    img,d=new_page()
    px=[16,440,864]; pw=400
    for i,X in enumerate(px):
        panel(d, X, 16, pw, 596, "LFO %d MOD"%(i+1))
        ctext(d, X+200, 58, "DEPTH MOD SOURCE", font(14,False), MUTED)
    save(img,"bg_lfomod.png")

# ENV key sub-pages: velocity / keyboard scaling for the two envelopes of that key
def page_vel(a,b,name):
    img,d=new_page()
    for y0,n in ((12,a),(320,b)):
        panel(d, 8, y0, 1264, 300, "ENV %d VELOCITY · KEYBOARD"%n)
    save(img,"bg_%s.png"%name)

# selector-grid cell images: one lit, one unlit (72 x 32, used by every tap grid)
CELL_W, CELL_H, CELL_GX, CELL_GY = 72, 32, 4, 4
def cell_images():
    # See-through: the caption Label sits UNDER the Button (so the button gets the touch),
    # and shows through an outline-only "off" cell and a translucent "on" cell.
    for name,fill,edge in (("cell_off.png",(27,37,42,0),BORDER+(255,)),("cell_on.png",CYAN+(105,),CYAN+(255,))):
        im=Image.new("RGBA",(CELL_W,CELL_H),(0,0,0,0)); dd=ImageDraw.Draw(im)
        dd.rounded_rectangle([0,0,CELL_W-1,CELL_H-1],radius=7,fill=fill,outline=edge,width=2)
        im.save(os.path.join(OUT,name))

# env pages (2-up)
# Every envelope is the MPC's live, touch-editable Envelope widget (DADDSR); no curve
# art is baked into the backgrounds. Confirmed on the Key 37 G2 2026-11-15.
ENV_CURVE_BOX = (24, 42, 536, 238)     # x, y offset inside the block, w, h

def env_block(d, y0, n, title):
    panel(d, 8, y0, 1264, 300, title)
    ctext(d, 696, y0+16, "LEVELS", font(14,False), MUTED)
    ctext(d, 1096, y0+16, "RATES", font(14,False), MUTED)

def page_env(a,b,name,fn):
    img,d=new_page()
    env_block(d, 12, a, "ENV %d"%a)
    env_block(d, 320, b, "ENV %d"%b)
    save(img,"bg_%s.png"%name)

# page 4 : FILTER
def page_filter():
    img,d=new_page()
    panel(d, 16, 16, 1248, 596, "FILTER")
    # no response-curve art: no MPC widget can draw it live, and static art misleads
    d.line([(40, 238), (1240, 238)], fill=BORDER, width=1)
    d.text((44, 246), "CUTOFF MOD 1", font=font(14,False), fill=MUTED)
    d.text((684, 246), "CUTOFF MOD 2", font=font(14,False), fill=MUTED)
    d.line([(40, 432), (1240, 432)], fill=BORDER, width=1)
    save(img,"bg_filter.png")

# page 5 : LFO
def page_lfo():
    img,d=new_page()
    px=[16,440,864]; pw=400
    kinds=["tri","saw","sqr"]
    for i,X in enumerate(px):
        panel(d, X, 16, pw, 596, "LFO %d"%(i+1))
        wave_glyph(d, X+290, 66, 80, 30, kinds[i])
        ctext(d, X+200, 210, "WAVE", font(14,False), MUTED)
    save(img,"bg_lfo.png")

# page 6 : MOD / TUNE
def page_mod():
    img,d=new_page()
    panel(d, 16, 16, 760, 596, "OUTPUT")
    panel(d, 792, 16, 472, 596, "GLOBAL")
    ctext(d, 396, 150, "MASTER / TUNE", font(15,False), MUTED)
    ctext(d, 1028, 150, "VOICE MODE", font(15,False), MUTED)
    # wordmark
    glow_text(d, 60, 470, BRAND, font(74 if len(BRAND) <= 5 else 62,True), CYAN)
    d.text((66, 556), TAGLINE, font=font(16,False), fill=MUTED)
    save(img,"bg_mod.png")

page_osc()
page_routes("bg_oscmod.png", "OSC %d PITCH MOD")
page_routes("bg_dcamod.png", "DCA %d LEVEL MOD")
page_lfomod()
page_vel(1,2,"vel12")
page_vel(3,4,"vel34")
cell_images()
page_env(1,2,"env12",1)
page_env(3,4,"env34",2)
page_filter()
page_lfo()
page_mod()
print("backgrounds written")

# =============================================================== KIT PNG COPY
USE_KIT = ["knob_uni.png","knob_bi.png","knob_small_bi.png",
           "fader_h_uni.png","fader_v_uni.png","toggle_on.png","toggle_off.png"]
# Film strips are 127 frames in the kit. A strip taller than 16384 px is over the image-size
# limit the Open MPC catalogue checks for (and a common texture limit), so a strip that would
# exceed it is thinned to evenly spaced frames. FRAMES records the count each strip ends up with.
MAX_STRIP_H = 16384
FRAMES = {}
for f in USE_KIT:
    src = os.path.join(KIT,f)
    im = Image.open(src)
    if im.height > MAX_STRIP_H and im.height % 127 == 0:
        fh = im.height // 127
        n = min(101, MAX_STRIP_H // fh)
        out = Image.new(im.mode, (im.width, fh*n))
        for k in range(n):
            j = round(k * 126 / (n - 1))
            out.paste(im.crop((0, j*fh, im.width, (j+1)*fh)), (0, k*fh))
        out.save(os.path.join(OUT,f)); FRAMES[f] = n
    else:
        shutil.copy(src, os.path.join(OUT,f)); FRAMES[f] = 127
print("kit pngs copied; frames:", {k:v for k,v in FRAMES.items() if v != 127})

# =============================================================== TUI.json builder
class Pairs(list):
    """ordered object that may carry duplicate keys (overlay version quirk)"""
    pass

def ser(o, ind=0):
    pad="  "*ind; pad2="  "*(ind+1)
    if isinstance(o, Pairs):
        if not o: return "{}"
        inner=",\n".join(pad2+json.dumps(k)+": "+ser(v,ind+1) for k,v in o)
        return "{\n"+inner+"\n"+pad+"}"
    if isinstance(o, dict):
        if not o: return "{}"
        inner=",\n".join(pad2+json.dumps(k)+": "+ser(v,ind+1) for k,v in o.items())
        return "{\n"+inner+"\n"+pad+"}"
    if isinstance(o, list):
        if not o: return "[]"
        inner=",\n".join(pad2+ser(v,ind+1) for v in o)
        return "[\n"+inner+"\n"+pad+"]"
    if isinstance(o, bool): return "true" if o else "false"
    if isinstance(o, float):
        return json.dumps(round(o,3))
    return json.dumps(o)

def act(on, handler, hn, add):
    return {"version":1,"onAction":on,"handler":handler,"handleName":hn,"additionalData":add}

def bg_transparent():
    return {"version":1,"focussed":{"version":1,"colour":"00000000","image":""},
            "unfocussed":{"version":1,"colour":"00000000","image":""}}

def focus_entry(w,h):
    return {"version":2,
        "componentData":{"version":1,"name":"focus","type":"Focus",
            "data":{"version":1,"backgroundColour":"19ffffff","outlineColour":"ff19c6e0",
                    "backgroundInset":4.0,"outlineThickness":1.0}},
        "handle remapping":{"version":1,"map":[]},
        "bounds":{"version":1,"acceptsHWFocus":"No","showWhenDataModelInvalid":"Show",
                  "whenVisible":"WhenFocussed","boundsType":"Absolute","bounds":"0 0 %d %d"%(w,h)}}

def child(name, typ, data, bounds, focus="No", invalid="Show"):
    return {"version":2,
        "componentData":{"version":1,"name":name,"type":typ,"data":data},
        "handle remapping":{"version":1,"map":[]},
        "bounds":{"version":1,"acceptsHWFocus":focus,"showWhenDataModelInvalid":invalid,
                  "whenVisible":"Always","boundsType":"Absolute","bounds":bounds}}

def lbl(text_type, size, colour, just, bold_style, hn="Data", text=None):
    data={"version":1,
        "textStyle":{"version":1,
            "font":{"version":1,"name":"Titillium Web","style":bold_style,"height":float(size)},
            "colour":colour,"justification":just,"case":"Original"},
        "type":text_type,"handleName":hn}
    if text is not None: data["text"]=text
    return data

def filmknob(img,bi=False):
    return {"version":2,"knobType":"FilmStrip","filmStrip":img,"numFrames":FRAMES[img],
            "invert":False,"handleName":"Data"}

# ---- reusable component definitions (my compact SQ-80 set) ----
def comp(actions, children):
    return {"version":2,"actions":actions,"backgroundData":bg_transparent(),
            "ignoreMousePresses":False,"disableCoarseDataWheel":False,
            "componentsData":children}

OPEN_KNOB=[act("Mouse Down","Q-Link","Data",""),
           act("Double Click","Show Overlay","Data","knob overlay"),
           act("Enter Pressed","Show Overlay","Data","knob overlay")]
OPEN_KNOB_BI=[act("Mouse Down","Q-Link","Data",""),
           act("Double Click","Show Overlay","Data","knob overlay bipolar"),
           act("Enter Pressed","Show Overlay","Data","knob overlay bipolar")]

def knob_comp(img, bipolar):
    opn = OPEN_KNOB_BI if bipolar else OPEN_KNOB
    return comp(opn,[
        focus_entry(262,106),
        child("knob","Knob",filmknob(img), "8 8 90 90"),
        child("name","Label",lbl("Name",24,"ff5c7078","left","Regular"), "104 14 150 34"),
        child("value","Label",lbl("Value",28,"ffe6f7fb","left","Regular"), "104 52 150 40"),
    ])

def knob_small_comp(img, bipolar):
    opn = OPEN_KNOB_BI if bipolar else OPEN_KNOB
    return comp(opn,[
        focus_entry(116,120),
        child("name","Label",lbl("Name",20,"ff5c7078","horizontallyCentred","Regular"),"0 0 116 24"),
        child("knob","Knob",filmknob(img),"30 26 56 56"),
        child("value","Label",lbl("Value",24,"ffe6f7fb","horizontallyCentred","Regular"),"0 86 116 28"),
    ])

def faderh_comp():
    return comp(OPEN_KNOB,[
        focus_entry(276,92),
        child("name","Label",lbl("Name",24,"ff5c7078","left","Regular"),"14 6 150 30"),
        child("value","Label",lbl("Value",26,"ffe6f7fb","right","Regular"),"150 6 118 30"),
        child("fader","Knob",{"version":3,"knobType":"FilmStrip","filmStrip":"fader_h_uni.png",
            "numFrames":FRAMES["fader_h_uni.png"],"invert":False,"dragOrientation":"Horizontal","handleName":"Data"},
            "14 46 248 34"),
    ])

def faderv_comp():
    return comp(OPEN_KNOB,[
        focus_entry(84,224),
        child("name","Label",lbl("Name",20,"ff5c7078","horizontallyCentred","Regular"),"0 0 84 22"),
        child("fader","Knob",filmknob("fader_v_uni.png"),"24 28 36 160"),
        child("value","Label",lbl("Value",22,"ffe6f7fb","horizontallyCentred","Regular"),"0 192 84 24"),
    ])

def toggle_comp():
    return comp([act("Mouse Down","Q-Link","Data",""),
                 act("Enter Pressed","Show Overlay","Data","knob overlay")],[
        focus_entry(276,104),
        child("name","Label",lbl("Name",24,"ff5c7078","left","Regular"),"12 10 160 32"),
        child("value","Label",lbl("Value",28,"ffe6f7fb","left","Regular"),"12 48 160 36"),
        child("toggle","Button",{"version":2,"onImage":"toggle_on.png","offImage":"toggle_off.png",
            "buttonId":1,"numButtonsInGroup":1,"handleName":"Data","gestureBehaviour":"Instant"},
            "182 20 84 68"),
    ])

def toggle_compact():   # 200 x 92: name over a toggle with its value text beside it
    return comp([act("Mouse Down","Q-Link","Data",""),
                 act("Enter Pressed","Show Overlay","Data","knob overlay")],[
        focus_entry(200,92),
        child("name","Label",lbl("Name",22,"ff5c7078","left","Regular"),"12 8 180 28"),
        child("toggle","Button",{"version":2,"onImage":"toggle_on.png","offImage":"toggle_off.png",
            "buttonId":1,"numButtonsInGroup":1,"handleName":"Data","gestureBehaviour":"Instant"},
            "12 44 64 36"),
        child("value","Label",lbl("Value",24,"ffe6f7fb","left","Regular"),"88 44 104 36"),
    ])

# Tap-to-pick selector: one lit button per choice (radio group) with its caption on top.
# Built from Button + Label, the well-tested widgets, because MenuOverlay rows render with
# no text on the Key 37 G2 / MPC 3.10.1.12 (plain, numbered and generator-exact overlays all
# blank, tested 2026-11-15). The Q-Link knob / data wheel step the same choices.
def grid_size(n, cols=4):
    rows=(n+cols-1)//cols
    return cols*CELL_W+(cols-1)*CELL_GX, rows*CELL_H+(rows-1)*CELL_GY

def grid_comp(captions, cols=4):
    n=len(captions); w,h=grid_size(n,cols)
    kids=[focus_entry(w,h)]
    pos=[((i%cols)*(CELL_W+CELL_GX), (i//cols)*(CELL_H+CELL_GY)) for i in range(n)]
    # Paint order matters: captions first, buttons last (topmost). With the captions on top
    # (tried 2026-11-15) a tap landed on the Label and never reached the Button under it:
    # the cells drew and lit correctly but could not be tapped.
    for i,(x,y) in enumerate(pos):
        kids.append(child("cap %d"%i,"Label",
            lbl("Text",18,"ffe6f7fb","horizontallyCentred verticallyCentred","Regular",text=captions[i]),
            "%d %d %d %d"%(x,y,CELL_W,CELL_H)))
    for i,(x,y) in enumerate(pos):
        kids.append(child("cell %d"%i,"Button",{"version":2,"onImage":"cell_on.png","offImage":"cell_off.png",
            "buttonId":i,"numButtonsInGroup":n,"handleName":"Data","gestureBehaviour":"Instant"},
            "%d %d %d %d"%(x,y,CELL_W,CELL_H)))
    return comp([act("Mouse Down","Q-Link","Data",""),
                 act("Enter Pressed","Show Overlay","Data","knob overlay")], kids)

def choices_of(param_name):
    return list(IDX2P[NAME2IDX[param_name]]["choices"])
SRC_W, SRC_H = grid_size(16)       # 300 x 140
WAVE_W, WAVE_H = grid_size(4)      # 300 x 32

def wave_comp():   # OSC wave: generic slider shows wave-name text, big
    return comp(OPEN_KNOB,[
        focus_entry(372,96),
        child("slider","Slider",{"version":1,"handleName":"Data"},"12 12 348 72"),
    ])

def overlay_comp(widget_type, data_pairs):
    return {"version":2,"actions":[],"backgroundData":bg_transparent(),
        "ignoreMousePresses":True,"disableCoarseDataWheel":False,
        "componentsData":[{"version":2,
            "componentData":{"version":1,"name":"overlay","type":widget_type,"data":data_pairs},
            "handle remapping":{"version":1,"map":[]},
            "bounds":{"version":1,"acceptsHWFocus":"Yes","showWhenDataModelInvalid":"Show",
                      "whenVisible":"Always","boundsType":"Absolute","bounds":"0 0 1280 800"}}]}

FOCUS_LOST=[act("Focus Lost","Focus Lost","Focus Lost","")]
def knob_overlay_data(polarity):
    return Pairs([("version",2),("version",1),("actions",FOCUS_LOST),
        ("overlayType","Knob With Header"),("scaleType","Linear"),
        ("polarityType",polarity),("showMinusSign",True),("handleName","Data")])
COMPONENTS=[
    ("knobU", knob_comp("knob_uni.png",False)),
    ("knobB", knob_comp("knob_bi.png",True)),
    ("knobSB", knob_small_comp("knob_small_bi.png",True)),
    ("faderH", faderh_comp()),
    ("faderV", faderv_comp()),
    ("toggleP", toggle_comp()),
    ("toggleC", toggle_compact()),
    ("srcGrid", grid_comp(choices_of("OSC1 Mod1 Src"))),
    ("waveRow", grid_comp(choices_of("LFO1 Wave"))),
    ("waveSlider", wave_comp()),
    ("knob overlay", overlay_comp("KnobOverlay", knob_overlay_data("Unipolar"))),
    ("knob overlay bipolar", overlay_comp("KnobOverlay", knob_overlay_data("Bipolar"))),
]

# ---------------------------------------------------------- control placement
# each control: (component, param_name, x, y, w, h)  w,h = focus/cell size
def P(nm): return "Parameter %d"%NAME2IDX[nm]

def ctl(comp, nm, x, y, w, h):
    return (comp, nm, x, y, w, h)

# --- OSC page cells ---
def osc_panel(X, o):
    # o = 1..3
    base = {1:(5,2,3,4,6,7), 2:(11,8,9,10,12,13), 3:(17,14,15,16,18,19)}
    # names
    nm = lambda i: IDX2P[i]["name"]
    wave,oct_,semi,fine,level,on = base[o]
    cells=[]
    cells.append(ctl("waveSlider", nm(wave), X+14, 62, 372, 96))
    cells.append(ctl("knobSB", nm(oct_),  X+16, 176, 116, 120))
    cells.append(ctl("knobSB", nm(semi),  X+142,176, 116, 120))
    cells.append(ctl("knobSB", nm(fine),  X+268,176, 116, 120))
    cells.append(ctl("faderH", nm(level), X+60, 332, 276, 92))
    cells.append(ctl("toggleP",nm(on),    X+60, 470, 276, 104))
    return cells

def env_cells(y0, n):
    nm=lambda s: "ENV%d %s"%(n,s)
    cells=[]
    # levels: 3 small bipolar knobs
    lx=[600,718,836]
    for i,xx in enumerate(lx):
        cells.append(ctl("knobSB", nm("L%d"%(i+1)), xx, y0+40, 116, 120))
    # times: 4 vertical faders
    tx=[958,1040,1122,1204]
    for i,xx in enumerate(tx):
        cells.append(ctl("faderV", nm("T%d"%(i+1)), xx, y0+36, 76, 224))
    return cells

def filter_cells():
    cells=[]
    # one row of four, centred on the four Q-Link columns; Glide under Cutoff
    cells.append(ctl("knobU","Cutoff",     31, 110, 262,106))
    cells.append(ctl("knobU","Reso",      351, 110, 262,106))
    cells.append(ctl("knobU","Key Track", 671, 110, 262,106))
    cells.append(ctl("knobU","Final Env", 991, 110, 262,106))
    # cutoff mod routes: source, depth, source, depth on the same four columns
    cells.append(ctl("srcGrid","Flt Mod1 Src",  31, 272, SRC_W, SRC_H))
    cells.append(ctl("knobB",  "Flt Mod1 Amt", 351, 290, 262,106))
    cells.append(ctl("srcGrid","Flt Mod2 Src", 671, 272, SRC_W, SRC_H))
    cells.append(ctl("knobB",  "Flt Mod2 Amt", 991, 290, 262,106))
    cells.append(ctl("knobU","Glide",      31, 456, 262,106))
    return cells

def route_panel(X, dest, o):
    # dest = "OSC" (pitch mod) or "DCA" (level mod); two routes, source over depth
    nm=lambda s: "%s%d %s"%(dest,o,s)
    return [ctl("srcGrid", nm("Mod1 Src"), X+50,  74, SRC_W, SRC_H),
            ctl("knobB",   nm("Mod1 Amt"), X+69, 218, 262,106),
            ctl("srcGrid", nm("Mod2 Src"), X+50, 356, SRC_W, SRC_H),
            ctl("knobB",   nm("Mod2 Amt"), X+69, 500, 262,106)]

def lfo_panel(X, l):
    base={1:(48,49,50,51),2:(52,53,54,55),3:(56,57,58,59)}
    rate,wave,depth,delay=base[l]
    nm=lambda i: IDX2P[i]["name"]
    cells=[]
    cells.append(ctl("knobU", nm(rate),  X+24, 92,  262,106))
    cells.append(ctl("waveRow", nm(wave), X+50,236, WAVE_W, WAVE_H))
    cells.append(ctl("knobU", nm(depth), X+24,330, 262,106))
    cells.append(ctl("knobU", nm(delay), X+24,452, 262,106))
    return cells

def lfomod_panel(X, l):
    nm=lambda s: "LFO%d %s"%(l,s)
    return [ctl("srcGrid", nm("Mod Src"), X+50,  80, SRC_W, SRC_H),    # scales LFO depth
            ctl("knobU",   nm("L2"),      X+69, 240, 262,106),         # level after the delay
            ctl("toggleC", nm("Reset"),   X+100,362, 200, 92),
            ctl("toggleC", nm("Human"),   X+100,462, 200, 92)]

def vel_cells(y0, n):
    nm=lambda s: "ENV%d %s"%(n,s)
    return [ctl("knobU",   nm("Vel Lvl"),    21, y0+110, 262,106),
            ctl("toggleC", nm("Vel Curve"), 296, y0+117, 200, 92),
            ctl("knobU",   nm("Vel Atk"),   509, y0+110, 262,106),
            ctl("knobU",   nm("Key Trk"),   784, y0+110, 262,106),
            ctl("toggleC", nm("2nd Rel"),  1059, y0+117, 200, 92)]

def mod_cells():
    cells=[]
    cells.append(ctl("faderH","Volume", 70, 200, 276,92))
    cells.append(ctl("waveSlider","Program", 70, 320, 372,96))
    cells.append(ctl("knobB","Pan", 471, 180, 262,106))
    cells.append(ctl("srcGrid","Pan Mod Src", 452, 296, SRC_W, SRC_H))
    cells.append(ctl("knobB","Pan Mod Amt", 471, 446, 262,106))
    for i,nm in enumerate(("Mono","Sync","AM","Env Cycle")):
        cells.append(ctl("toggleC", nm, 812, 190+100*i, 200, 92))
    for i,nm in enumerate(("Voice Restart","Env Restart","Osc Restart")):
        cells.append(ctl("toggleC", nm, 1036, 190+100*i, 200, 92))
    return cells

# assemble pages: (tabName, fnKeyIndex, fnKeySubIndex, bg_image, cells[])
# Pressing a function key again cycles its sub-pages (OSC -> OSC MOD -> DCA MOD).
OSC_CELLS = osc_panel(16,1)+osc_panel(440,2)+osc_panel(864,3)
OSCMOD_CELLS = route_panel(16,"OSC",1)+route_panel(440,"OSC",2)+route_panel(864,"OSC",3)
DCAMOD_CELLS = route_panel(16,"DCA",1)+route_panel(440,"DCA",2)+route_panel(864,"DCA",3)
ENV12_CELLS = env_cells(12,1)+env_cells(320,2)
ENV34_CELLS = env_cells(12,3)+env_cells(320,4)
FILTER_CELLS = filter_cells()
LFO_CELLS = lfo_panel(16,1)+lfo_panel(440,2)+lfo_panel(864,3)
LFOMOD_CELLS = lfomod_panel(16,1)+lfomod_panel(440,2)+lfomod_panel(864,3)
VEL12_CELLS = vel_cells(12,1)+vel_cells(320,2)
VEL34_CELLS = vel_cells(12,3)+vel_cells(320,4)
MOD_CELLS = mod_cells()

PAGES=[
    ("OSC",      0, 0, "bg_osc.png",    OSC_CELLS),
    ("OSC MOD",  0, 1, "bg_oscmod.png", OSCMOD_CELLS),
    ("DCA MOD",  0, 2, "bg_dcamod.png", DCAMOD_CELLS),
    ("ENV 1·2", 1, 0, "bg_env12.png", ENV12_CELLS),
    ("VEL 1·2", 1, 1, "bg_vel12.png", VEL12_CELLS),
    ("ENV 3·4", 2, 0, "bg_env34.png", ENV34_CELLS),
    ("VEL 3·4", 2, 1, "bg_vel34.png", VEL34_CELLS),
    ("FILTER",   3, 0, "bg_filter.png", FILTER_CELLS),
    ("LFO",      4, 0, "bg_lfo.png",    LFO_CELLS),
    ("LFO MOD",  4, 1, "bg_lfomod.png", LFOMOD_CELLS),
    ("MOD·TUNE", 5, 0, "bg_mod.png",  MOD_CELLS),
]

# ---- live envelope (MPC Envelope widget, DADDSR) ----
# SQ-80 stage -> DADDSR handle:  T1 attack | T2 decay1 | L2 level1 | T3 decay |
# L3 sustain | T4 release.  L1 has no slot (the widget draws the attack peak at full
# scale), so it stays on its knob.  DADDSR's delay stage is pinned to a zero constant.
# Key order follows docs/ui-reference.md 6.13 (the parser is order-sensitive).
FIXED_ZERO = ("Fixed/displayRange 0 1 valueRange 0 1 fineIncrement 0.01 coarseIncrement 0.1 "
              "defaultValue 0 displayString '0' name 'Delay' value 0")

def env_widget(y0, n):
    p=lambda s: P("ENV%d %s"%(n,s))
    x,dy,w,h = ENV_CURVE_BOX
    data={"version":1,
        "envelopeType":"DADDSR","envelopeStyle":"WithoutHeader","envelopeName":"ENV %d"%n,
        "backgroundColour":"ff0c1215","scaleColour":"ff1e363c",
        "lineColour":"ff19c6e0","fillColour":"3019c6e0","highlightColour":"ffaaf5ff",
        "lineWidth":3,"handleWidth":18,"handleHeight":14,"handleShape":"Circle",
        "handleLineWidth":2,"handleLineColour":"ffe6f7fb","handleFillColour":"ff10161a",
        "handleTextColour":"ffe6f7fb",
        "handleLineWidthOnPressed":2,"handleLineColourOnPressed":"ffe6f7fb",
        "handleFillColourOnPressed":"ff19c6e0","handleTextColourOnPressed":"ffe6f7fb",
        "handleArrowLineColourOnPressed":"ffe6f7fb","handleArrowFillColourOnPressed":"ff19c6e0",
        "delayHandle":FIXED_ZERO,
        "attackHandle":p("T1"),
        "decay1Handle":p("T2"),"level1Handle":p("L2"),
        "decayHandle":p("T3"),
        "sustainHandle":p("L3"),
        "releaseHandle":p("T4"),
        "attackShapeHandle":"Invalid","decay1ShapeHandle":"Invalid",
        "decayShapeHandle":"Invalid","releaseShapeHandle":"Invalid",
        "forceHideShapeHandles":True,"forceCurveShape":0.5,"curveType":"Quadratic"}
    return child("env%d curve"%n,"Envelope",data,"%d %d %d %d"%(x,y0+dy,w,h))

# extra (non-cell) children per tab name: the live envelopes
ENV_Y0 = {1:12, 2:320, 3:12, 4:320}
EXTRA = {"ENV 1·2":[env_widget(ENV_Y0[n],n) for n in (1,2)],
         "ENV 3·4":[env_widget(ENV_Y0[n],n) for n in (3,4)]}

# ---------------------------------------------------------- Q-Link banks per page
# One bank = one group of four knobs = one highlighted rectangle. Each bank follows a
# panel (or a block inside it) so the knobs under the hand match what is lit on screen.
# Entry: (focus rect "x y w h", [up to 4 parameter names, in knob order]).
# Controls not listed are touch-only. Unused banks get an empty rect and no parameters.
NO_BANK = ("0 0 0 0", [])
OSC_X = (16, 440, 864)
def _osc_bank(X, o):
    return ("%d 56 384 246"%(X+8), ["OSC%d Wave"%o, "OSC%d Oct"%o, "OSC%d Semi"%o, "OSC%d Fine"%o])
def _env_banks(y0, n):
    return [("594 %d 364 132"%(y0+34), ["ENV%d L1"%n, "ENV%d L2"%n, "ENV%d L3"%n]),
            ("954 %d 320 236"%(y0+30), ["ENV%d T1"%n, "ENV%d T2"%n, "ENV%d T3"%n, "ENV%d T4"%n])]
def _lfo_bank(X, l):
    return ("%d 86 336 478"%(X+18), ["LFO%d Rate"%l, "LFO%d Wave"%l, "LFO%d Depth"%l, "LFO%d Delay"%l])
def _lfomod_bank(X, l):
    return ("%d 72 384 490"%(X+8), ["LFO%d Mod Src"%l, "LFO%d L2"%l, "LFO%d Reset"%l, "LFO%d Human"%l])
def _vel_banks(y0, n):
    return [("13 %d 1040 122"%(y0+102), ["ENV%d Vel Lvl"%n, "ENV%d Vel Curve"%n, "ENV%d Vel Atk"%n, "ENV%d Key Trk"%n]),
            ("1053 %d 212 122"%(y0+102), ["ENV%d 2nd Rel"%n])]
def _route_bank(X, dest, o):
    return ("%d 68 384 542"%(X+8), ["%s%d Mod%d %s"%(dest,o,r,k) for r in (1,2) for k in ("Src","Amt")])

QBANKS = {
    # three pitch blocks, then the three DCA level faders as one "mixer" bank
    "OSC":      [_osc_bank(X, i+1) for i, X in enumerate(OSC_X)]
                + [("70 326 1136 104", ["OSC1 Level", "OSC2 Level", "OSC3 Level"])],
    "ENV 1·2":  _env_banks(ENV_Y0[1], 1) + _env_banks(ENV_Y0[2], 2),
    "ENV 3·4":  _env_banks(ENV_Y0[3], 3) + _env_banks(ENV_Y0[4], 4),
    "OSC MOD":  [_route_bank(X, "OSC", i+1) for i, X in enumerate(OSC_X)] + [NO_BANK],
    "DCA MOD":  [_route_bank(X, "DCA", i+1) for i, X in enumerate(OSC_X)] + [NO_BANK],
    "FILTER":   [("25 104 1230 118", ["Cutoff", "Reso", "Key Track", "Final Env"]),
                 ("25 266 1230 152", ["Flt Mod1 Src", "Flt Mod1 Amt", "Flt Mod2 Src", "Flt Mod2 Amt"]),
                 ("25 450 274 118", ["Glide"]), NO_BANK],
    "LFO":      [_lfo_bank(X, i+1) for i, X in enumerate(OSC_X)] + [NO_BANK],
    "LFO MOD":  [_lfomod_bank(X, i+1) for i, X in enumerate(OSC_X)] + [NO_BANK],
    "VEL 1·2":  _vel_banks(ENV_Y0[1], 1) + _vel_banks(ENV_Y0[2], 2),
    "VEL 3·4":  _vel_banks(ENV_Y0[3], 3) + _vel_banks(ENV_Y0[4], 4),
    "MOD·TUNE": [("62 192 376 232", ["Volume", "Program"]),
                 ("444 172 316 388", ["Pan", "Pan Mod Src", "Pan Mod Amt"]),
                 ("804 182 216 408", ["Mono", "Sync", "AM", "Env Cycle"]),
                 ("1028 182 216 308", ["Voice Restart", "Env Restart", "Osc Restart"])],
}
for _name, _fn, _sub, _bg, _cells in PAGES:
    _on_page = {c[1] for c in _cells}
    assert len(QBANKS[_name]) == 4, _name
    for _rect, _names in QBANKS[_name]:
        assert len(_names) <= 4 and all(n in _on_page for n in _names), (_name, _names)

def tab_entry(name, fn, sub, bg, cells):
    children=[]
    # full-screen background image first
    children.append(child("bg","Image",
        {"version":2,"imageType":"Regular","colour":"00000000","image":bg},
        "0 0 1280 628"))
    children.extend(EXTRA.get(name, []))
    for comp,nm,x,y,w,h in cells:
        e={"version":2,
           "componentData":{"version":1,"name":nm,"type":comp,
                "data":{"version":1,"handleName":"Data"}},
           "handle remapping":{"version":1,"map":[{"key":"Data","value":P(nm)}]},
           "bounds":{"version":1,"acceptsHWFocus":"Yes","showWhenDataModelInvalid":"Hide",
                     "whenVisible":"Always","boundsType":"Absolute",
                     "bounds":"%d %d %d %d"%(x,y,w,h)}}
        children.append(e)
    root={"version":2,"actions":[],
        "backgroundData":{"version":1,
            "focussed":{"version":1,"colour":"ff10161a","image":""},
            "unfocussed":{"version":1,"colour":"ff10161a","image":""}},
        "ignoreMousePresses":False,"disableCoarseDataWheel":False,
        "componentsData":children}
    return {"version":1,"tabName":name,"fnKeyIndex":fn,"fnKeySubIndex":sub,
            "componentDefinition":root,
            "qlinkBoundsData":[rect for rect,_ in QBANKS[name]]}

# Short choice lists (LFO waves, the 16 mod sources) -> replacementStrings, which give the
# controls their discrete steps. OSC Wave (75 names) is left continuous.
repl=[]
for prm in PLIST:
    if prm.get("kind")=="choice" and 2 <= len(prm.get("choices",[])) <= 16:
        repl.append({"key":"Parameter %d"%prm["index"],"value":list(prm["choices"])})

TUI={"pageData":{
    "version":1,
    "componentDefinitions":{"version":2,"importFiles":[],
        "localComponentDefinitions":[{"key":k,"value":v} for k,v in COMPONENTS]},
    "replacementStrings":repl,
    "info":{"version":1,"type":"CompleteDescription"},
    "tabs":[tab_entry(*p) for p in PAGES],
}}

with open(os.path.join(OUT,"TUI.json"),"w") as f:
    f.write(ser(TUI))
print("TUI.json written")

# =============================================================== Q-Links.json
# Bank b (0..3) is column b of the 4 x 4 knob grid, read top to bottom: 13+b, 9+b, 5+b, 1+b
# (Bank Direction "Column"). On 4-knob hardware each bank is one knob set.
def qlinks_for(name):
    q={"Q-Link %d"%i:-1 for i in range(1,17)}
    for b,(_,names) in enumerate(QBANKS[name]):
        for k,nm in enumerate(names):
            q["Q-Link %d"%(13+b-4*k)]=NAME2IDX[nm]
    return q

qmap=[]
for name,fn,sub,bg,cells in PAGES:
    qmap.append({"Tab":fn+1,"SubTab":sub+1,"Bank Direction":"Column","Q-Links":qlinks_for(name)})

# every parameter must be reachable on some page
_placed={c[1] for pg in PAGES for c in pg[4]}
_missing=[q["name"] for q in PLIST if q["name"] not in _placed]
assert not _missing, ("parameters with no control", _missing)

# program-mode: 16 curated important params
PROG=["Volume","Cutoff","Reso","Final Env","Key Track","Glide","Pan","OSC1 Level",
      "OSC2 Level","OSC3 Level","OSC1 Wave","OSC2 Wave","OSC3 Wave","LFO1 Rate",
      "LFO1 Depth","Program"]
prog={"Q-Link %d"%(i+1): NAME2IDX[PROG[i]] for i in range(16)}

QLINKS={"version":4,"info":{"version":1,"type":"CompleteDescription"},
    "Screen Mode Q-Links":{"version":4,"map":qmap},
    "Program Mode Q-Links":prog}
with open(os.path.join(OUT,"Q-Links.json"),"w") as f:
    f.write(ser(QLINKS))
print("Q-Links.json written")

# sanity: reparse (validate.py tolerates dup keys; standard json keeps last)
json.load(open(os.path.join(OUT,"TUI.json")))
json.load(open(os.path.join(OUT,"Q-Links.json")))
print("json reparse OK; files in", OUT)
print("files:", sorted(os.listdir(OUT)))
