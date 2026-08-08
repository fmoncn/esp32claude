#!/usr/bin/env python3
"""生成 Clawd 小螃蟹精灵 — 完整动画版。
在用户提供的权威 SVG (16x11) 基础上, 加入原生级别的动画参数:
  armL/armR: 蟹钳角度(0-90), 驱动钳子大幅摆动
  eye: open/closed/sleep/happy/angry/wide/love 表情
  mouth: smile/open/flat (嘴巴)
  bob: 呼吸浮动
  legs: 腿部摆动
让 Clawd 像原生克劳德一样自然动起来 + 有丰富表情。
"""
import json, math, os
from PIL import Image, ImageDraw

FW, FH = 64, 72
OUT = "sprites_src"
SHEETS = "sim/sheets"

BODY   = (234, 110, 85, 255)   # 珊瑚橙 #EA6E55
SHADE  = (196, 92, 62, 255)    # 深橙(阴影/钳子连接)
EYE    = (40, 30, 28, 255)     # 深棕黑(眼睛/嘴)
WHITE  = (250, 250, 250, 255)
GOLD   = (255, 200, 90, 255)
PINK   = (255, 120, 160, 255)
RED    = (255, 90, 80, 255)
GREEN  = (130, 230, 150, 255)
SKY    = (120, 200, 240, 255)

# 权威 Clawd 16x11 结构 (O=身体/臂/腿, E=眼睛, .=背景)
GRID = [
    "..OOOOOOOOOOOO..",
    "..OOOOOOOOOOOO..",
    "..OOEOOOOOOEOO..",
    "..OOEOOOOOOEOO..",
    "OOOOEOOOOOOEOOOO",
    "OOOOEOOOOOOEOOOO",
    "..OOOOOOOOOOOO..",
    "..OOOOOOOOOOOO..",
    "..OOOOOOOOOOOO..",
    "...O.O....O.O...",
    "...O.O....O.O...",
]
GW, GH = 16, 11
PX = 4

ACTIONS = [
    ("idle",6,6,True,"idle"),("walking",6,10,True,"walk"),("running",6,14,True,"run"),
    ("jumping",6,9,False,"jump"),("flying",6,10,True,"fly"),("landing",6,9,False,"land"),
    ("blast",6,10,False,"beam"),("chest_blast",8,9,False,"beam"),("missile_launch",6,10,False,"beam"),
    ("laser_attack",6,10,False,"beam"),("charging",8,10,True,"charge"),("victory",6,8,False,"cheer"),
    ("low_battery",6,4,True,"droop"),("damaged",6,12,True,"shake"),("repairing",6,8,False,"tool"),
    ("upgrading",8,10,False,"charge"),("dancing",8,10,True,"dance"),("eating",6,7,False,"eat"),
    ("drinking",6,7,False,"eat"),("sleeping",6,4,True,"sleep"),("exercising",6,8,True,"cheer"),
    ("reading",6,4,True,"think"),("playing",6,9,True,"cheer"),("waving",6,10,False,"wave"),
    ("hugging",6,7,True,"love"),("giving_heart",6,7,False,"love"),("romantic",6,6,True,"love"),
    ("happy",6,9,True,"cheer"),("sad",6,5,True,"droop"),("angry",6,14,True,"shake"),
    ("surprised",6,9,False,"jump"),("thinking",6,5,True,"think"),("sleepy",6,5,True,"droop"),
    ("in_love",6,7,True,"love"),
]

def px(d, x, y, w, h, color):
    """x,y 为像素坐标; w,h 为像素宽高"""
    d.rectangle([x, y, x+w-1, y+h-1], fill=color)

def _heart(d, x, y, s, col):
    d.ellipse([x-s, y-s, x, y], fill=col)
    d.ellipse([x, y-s, x+s, y], fill=col)
    d.polygon([(x-s, y-s//2), (x+s, y-s//2), (x, y+s)], fill=col)

def draw_clawd(t, bob=0, eye="open", mouth="-", armL=0, armR=0, lean=0, tint=None, legs=0):
    """绘制 Clawd 螃蟹, 支持完整动画参数。
    armL/armR: 蟹钳角度(0..90), 0=水平, 90=举起
    eye: open/closed/sleep/happy/angry/wide/love
    mouth: smile/open/flat/-
    """
    im = Image.new("RGBA", (FW, FH), (0,0,0,0))
    d = ImageDraw.Draw(im)
    body = tint or BODY
    ox = 0 + lean
    oy = 14 + bob

    # 身体(行0-8, 列2-13) 用 body 色
    for gy in range(0,9):
        for gx in range(2,14):
            px(d, ox+gx*PX, oy+gy*PX, PX, PX, body)

    # 腿部(行9-10, 列3,5,10,12): legs 驱动明显踏步
    # legs>0: 左腿(列3,5)抬起变短, 右腿(列10,12)正常; legs<0 相反
    leg_cols = [3,5,10,12]
    for li, lx in enumerate(leg_cols):
        is_left = li < 2
        # 该腿的抬起量: 同侧+legs, 对侧-legs, 取正
        lift = legs if is_left else -legs
        # 行9 是腿顶, 行10 是腿底
        # 抬起的腿: 行9向上移(lift格), 腿变短
        row9 = 9 - (lift if lift > 0 else 0)
        px(d, ox+lx*PX, oy+row9*PX, PX, PX, body)  # 腿顶(上移=踏步)
        px(d, ox+lx*PX, oy+10*PX, PX, PX, body)    # 腿底始终在行10

    # 手臂(钳): 列0-1(左) 14-15(右), 行4-5, 角度驱动大幅摆动
    # armR>0 右钳举起, armL>0 左钳举起; 幅度0-3格
    def draw_arm(side, ang):
        # side: -1(左) +1(右); ang: 0..90 角度
        off = int(ang / 90 * 3)  # 0..3 格上抬(明显)
        if side == -1:
            for gy in (4,5):
                px(d, ox+0*PX, oy+(gy-off)*PX, PX, PX, body)
                px(d, ox+1*PX, oy+(gy-off)*PX, PX, PX, body)
        else:
            for gy in (4,5):
                px(d, ox+14*PX, oy+(gy-off)*PX, PX, PX, body)
                px(d, ox+15*PX, oy+(gy-off)*PX, PX, PX, body)
    draw_arm(-1, armL)
    draw_arm(1, armR)

    # 眼睛(行2-5, 列4 和 列11) 表情
    def draw_eye(ec):
        if eye == "closed" or eye == "sleep":
            # 闭眼: 横线(明显)
            px(d, ox+ec*PX, oy+3*PX, PX, PX//3, EYE)
        elif eye == "happy":
            # 开心: 眯眼横线
            px(d, ox+ec*PX, oy+3*PX, PX, PX//3, EYE)
        elif eye == "angry":
            # 生气: 红色
            px(d, ox+ec*PX, oy+2*PX, PX, PX, RED)
        elif eye == "wide":
            # 惊讶: 白底大眼
            px(d, ox+ec*PX, oy+2*PX, PX, 2*PX, WHITE)
            px(d, ox+ec*PX+PX//2, oy+3*PX, PX//2, PX//2, EYE)
        elif eye == "love":
            _heart(d, ox+ec*PX+PX//2, oy+3*PX+PX//2, PX//2, PINK)
        else:  # open: 黑色竖条(明显)
            px(d, ox+ec*PX, oy+2*PX, PX, 3*PX, EYE)

    draw_eye(4)
    draw_eye(11)

    # Clawd 无嘴巴(极简设计) - 不绘制嘴

    return im, d, ox, oy


def frame(kind, i, n):
    t = i/n
    s = math.sin(t*2*math.pi)
    if kind == "idle":
        # 呼吸浮动(明显) + 眨眼 + 钳子微摆
        bob=[0,0,-2,-2,0,0][i%6]
        eye="closed" if i==n-1 else "open"
        armL=[8,14,8,8,14,8][i%6]
        im,d,ox,oy = draw_clawd(t, bob=bob, eye=eye, mouth="smile", armL=armL, armR=armL)
    elif kind == "walk":
        sw=[0,1,0,-1,0,1][i%6]
        im,d,ox,oy = draw_clawd(t, bob=abs(sw), eye="open", mouth="-",
                                armL=30*sw, armR=-30*sw, legs=sw*2)
    elif kind == "run":
        sw=[0,2,0,-2,0,2][i%6]
        im,d,ox,oy = draw_clawd(t, bob=abs(sw)*2, eye="wide", mouth="open",
                                armL=50*sw, armR=-50*sw, legs=sw*3)
    elif kind == "jump":
        up=[-1,-4,-7,-5,-2,0][i%6]
        im,d,ox,oy = draw_clawd(t, bob=up, eye="wide", mouth="open", armL=70, armR=70)
    elif kind == "land":
        up=[0,-2,-3,-1,0,0][i%6]
        im,d,ox,oy = draw_clawd(t, bob=up, eye="open", mouth="open", armL=40, armR=40)
    elif kind == "fly":
        im,d,ox,oy = draw_clawd(t, bob=int(2*s)-2, eye="open", mouth="smile", armL=60, armR=60)
        px(d, ox+7*PX, oy+11*PX, 2*PX, PX//2, GOLD)
    elif kind == "beam":
        # 发射后坐力 + 光束伸长
        kick=[0,-1,-2,-2,-1,0][i%6]
        im,d,ox,oy = draw_clawd(t, bob=kick, eye="angry", mouth="open", armL=30, armR=30)
        if i>=2:
            col = PINK if i%2 else SKY
            h = 3 + (i%3)*2
            px(d, ox+7*PX, oy-2-i%3, 2*PX, h*PX, col)
    elif kind == "charge":
        glow = GOLD if i%2 else (255,225,130,255)
        im,d,ox,oy = draw_clawd(t, tint=glow, eye="wide", mouth="open", armL=30, armR=30)
        px(d, ox+6*PX, oy+8*PX, 4*PX, PX//2, GOLD)
    elif kind == "cheer":
        up=[0,-1,-2,-1][i%4]
        im,d,ox,oy = draw_clawd(t, bob=up, eye="happy", mouth="smile", armL=80, armR=80)
        if i%2: _heart(d, ox+14*PX, oy-2, 4, GOLD)
    elif kind == "dance":
        sw=[0,2,0,-2,0,2,0,-2][i%8]
        im,d,ox,oy = draw_clawd(t, bob=abs(sw), eye="happy", mouth="smile", armL=70, armR=-70, legs=sw)
    elif kind == "droop":
        bob=[1,2,1,2,1,2][i%6]
        im,d,ox,oy = draw_clawd(t, bob=bob, eye="sleep", mouth="flat", armL=10, armR=10)
    elif kind == "shake":
        im,d,ox,oy = draw_clawd(t, lean=[-2,2,-2,2,-1,1][i%6], eye="angry", mouth="open",
                                armL=40, armR=40, tint=RED if i%2 else None)
    elif kind == "tool":
        bob=[0,1,0,1,0,0][i%6]
        im,d,ox,oy = draw_clawd(t, bob=bob, eye="open", mouth="flat", armL=60, armR=10)
        px(d, ox, oy+4*PX, PX, 2*PX, GOLD)
    elif kind == "eat":
        bob=[0,0,-1,0,0,-1][i%6]
        im,d,ox,oy = draw_clawd(t, bob=bob, eye="happy", mouth="open", armL=40, armR=40)
        px(d, ox+3*PX, oy+7*PX+(i%2), PX, PX, GREEN)
    elif kind == "sleep":
        im,d,ox,oy = draw_clawd(t, eye="sleep", mouth="flat", armL=5, armR=5)
        d.text((ox+11*PX, oy-8-i%2), "z", fill=WHITE)
    elif kind == "think":
        im,d,ox,oy = draw_clawd(t, eye="open", mouth="flat", armL=20, armR=20)
        d.text((ox+11*PX, oy-8-i%2), "?", fill=GOLD)
    elif kind == "wave":
        ar=[0,30,60,30,0,0][i%6]
        im,d,ox,oy = draw_clawd(t, bob=[0,-1,0,-1,0,0][i%6], eye="happy", mouth="smile", armL=10, armR=ar)
    elif kind == "love":
        im,d,ox,oy = draw_clawd(t, bob=int(s), eye="love", mouth="smile", armL=40, armR=40)
        _heart(d, ox+8*PX, oy-10-(i%3)*4, 5, PINK)
    else:
        im,d,ox,oy = draw_clawd(t, bob=int(s), eye="open", mouth="smile", armL=8, armR=8)
    return im


def main():
    order=[a[0] for a in ACTIONS]
    meta={"frame_w":FW,"frame_h":FH,"batches":{"all":order},"actions":{}}
    os.makedirs(SHEETS, exist_ok=True)
    for name,frames,fps,loop,kind in ACTIONS:
        meta["actions"][name]={"frames":frames,"fps":fps,"loop":loop}
        fdir=os.path.join(OUT,"frames",name); os.makedirs(fdir,exist_ok=True)
        sheet=Image.new("RGBA",(FW*frames,FH),(0,0,0,0))
        for i in range(frames):
            im=frame(kind,i,frames)
            im.save(os.path.join(fdir,f"{name}_{i}.png"))
            sheet.alpha_composite(im,(i*FW,0))
        sheet.save(os.path.join(SHEETS,f"{name}.png"))
    json.dump(meta,open(os.path.join(OUT,"metadata.json"),"w"),ensure_ascii=False,indent=2)
    print(f"generated {len(order)} Clawd actions -> {OUT}/")

if __name__=="__main__":
    main()
