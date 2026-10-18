// =====================================================
// ESP32 SuperMini 外壳
//
// 结构:
//   - 主体: 顶部开口盒子, 顶面平齐无阴角
//   - 盖子: 板 + 薄凸缘, 凸缘搭在盒体顶边内侧定位
//
// 打印建议:
//   - 主体: 敞口朝上打印, 无需支撑
//   - 盖子: 平面朝下打印, 无需支撑; 文字凸起朝上位于最顶层
//   - 材料: PLA/PETG, 0.2mm 层高, 3 回墙
// =====================================================

// ============== 可调参数 ==============

// 内腔尺寸 (盒内)
INNER_L = 48; // 内腔长度 (mm)
INNER_W = 36; // 内腔宽度 (mm), USB 开口所在侧
INNER_H = 20; // 内腔高度 (mm), 四壁由 17.5 加高 2.5

// 壁厚
WALL = 1.5; // 壁厚 (mm)

// USB-C 开口 (从盒内测量, 位于 X=0 窄板即 INNER_W=36mm 侧)
//   "水平" 指面向该侧板时的左右方向 (对应 Y 轴)
//   "垂直" 指面向该侧板时的上下方向 (对应 Z 轴)
USB_H1 = 9; // 水平起始位置 (mm)
USB_H2 = 20; // 水平结束位置 (mm)
USB_V1 = 12.5; // 垂直起始位置, 从底面 (mm), 壁加高后保持不动
USB_V2 = 20; // 垂直结束位置, 从底面 (mm), 随壁同步加高 2.5 (仍与壁顶齐平)

// 盖子定位凸缘 (搭在盒体顶边内侧)
RIM_T = 0; // 凸缘厚度 (mm), 0 = 与盒体内壁齐平，无名义间隙
RIM_H = 1.5; // 凸缘高度 (mm), 插入盒体顶部的深度

// 盖子
LID_T = 1.2; // 盖板厚度 (mm)

// 盖子突起文字
MARK_TEXT1 = "AgentLight"; // 第一行文字
MARK_TEXT2 = "NianXi"; // 第二行文字
MARK_FONT = "Segoe Script"; // 字体 (Windows 手写体: Ink Free / Segoe Script / Brush Script MT)
MARK_SIZE = 5; // 字号 (mm)
MARK_WEIGHT = 0.25; // 字体粗细 (mm): 笔画外扩量, 0=原始, 值越大字体越粗
MARK_DEPTH = 0.8; // 凸起高度 (mm)
MARK_MARGIN = 3; // 角落边距 (mm)
MARK_GAP = 3; // 行间距 (mm)

// 盖子开缝 (从盒内测量)
SLOT_X1 = 26; // 缝隙 X 起始 (mm)
SLOT_Y1 = 6; // 缝隙 Y 起始 (mm)
SLOT_Y2 = 11; // 缝隙 Y 结束 (mm)

// 盒体右侧壁开口 (与盖子开缝 Y 位置/宽度对齐, 切穿右侧壁)
SIDE_SLOT_H = 12.5; // 开口高度 (mm), 随壁加高同步 +2.5 ("top" 模式下底边绝对位置不变)
SIDE_SLOT_V = "top"; // 垂直位置: "top"=贴顶(顶端与右壁顶边 OUTER_H 齐平), "center"=居中, 或数值=从底面起的起始高度 (mm)

// 间隙余量
TOL = 0.2; // 单侧间隙 (mm)

// 圆角
CORNER_R = 2; // 圆角半径 (0 = 直角)

// ============== 计算值 ==============

OUTER_L = INNER_L + 2 * WALL;
OUTER_W = INNER_W + 2 * WALL;
OUTER_H = INNER_H + WALL; // 底板壁厚 + 内腔

// USB 开口尺寸 (由参数联动)
USB_CUT_HW = USB_H2 - USB_H1; // 开口水平宽度
USB_CUT_VH = USB_V2 - USB_V1; // 开口垂直高度

// 盖子凸缘尺寸 (由参数联动)
TONGUE_L = INNER_L - 2 * RIM_T; // 凸缘长度 (内侧留间隙)
TONGUE_W = INNER_W - 2 * RIM_T; // 凸缘宽度 (内侧留间隙)
TONGUE_H = RIM_H; // 凸缘高度

// 盖子开缝尺寸 (由参数联动)
SLOT_X2 = OUTER_L; // 缝隙延伸到盖子右边缘
SLOT_CUT_L = SLOT_X2 - (WALL + SLOT_X1); // 缝隙长度
SLOT_CUT_W = SLOT_Y2 - SLOT_Y1; // 缝隙宽度

// 盒体右侧壁开口尺寸 (由参数联动)
//   宽度与盖子开缝一致 (SLOT_CUT_W), Y 位置对齐 (WALL + SLOT_Y1)
SIDE_SLOT_Z =
  (SIDE_SLOT_V == "top") ? OUTER_H - SIDE_SLOT_H // 贴顶: 顶端与右壁顶边齐平, 与盖子开缝上下连通
  : (SIDE_SLOT_V == "center") ? (OUTER_H - SIDE_SLOT_H) / 2 // 垂直居中
  : SIDE_SLOT_V; // 手动指定起始高度

$fn = 32;

// ============== 渲染控制 ==============
// "box" = 仅主体, "lid" = 仅盖子, "all" = 装配, "exploded" = 分解
PART = "box";

// =====================================================
// 公共: 圆角矩形
// =====================================================

module rounded_box(l, w, h, r) {
  if (r <= 0) {
    cube([l, w, h]);
  } else {
    hull() {
      translate([r, r, 0]) cylinder(r=r, h=h);
      translate([l - r, r, 0]) cylinder(r=r, h=h);
      translate([r, w - r, 0]) cylinder(r=r, h=h);
      translate([l - r, w - r, 0]) cylinder(r=r, h=h);
    }
  }
}

// =====================================================
// 主体 (顶部开口盒子, 无阴角)
// =====================================================

module box_body() {
  // 外壳 (顶部开口, 无阴角)
  difference() {
    rounded_box(OUTER_L, OUTER_W, OUTER_H, CORNER_R);

    // 主内腔 (从底板顶面到盒子顶部, 无台阶)
    translate([WALL, WALL, WALL])
      cube([INNER_L, INNER_W, INNER_H + 1]);
  }
}

module box_usb_cutout() {
  // USB-C 开口 (X=0 侧板, 即 INNER_W=36mm 窄板)
  //   水平方向 → Y 轴, 垂直方向 → Z 轴, 切穿 X 方向壁厚
  translate([-1, WALL + USB_H1, WALL + USB_V1])
    cube([WALL + 2, USB_CUT_HW, USB_CUT_VH]);
}

module box_side_cutout() {
  // 盒体右侧壁 (X=OUTER_L) 开口
  //   Y 位置/宽度与盖子开缝对齐, 高度 SIDE_SLOT_H, 切穿右侧壁
  translate([OUTER_L - WALL - 1, WALL + SLOT_Y1, SIDE_SLOT_Z])
    cube([WALL + 2, SLOT_CUT_W, SIDE_SLOT_H]);
}

module box() {
  difference() {
    box_body();
    box_usb_cutout();
    box_side_cutout();
  }
}

// =====================================================
// 盖子 (盖板 + 薄定位凸缘)
// =====================================================

module lid() {
  difference() {
    union() {
      // 盖板 (覆盖主体顶部)
      rounded_box(OUTER_L, OUTER_W, LID_T, CORNER_R);

      // 薄凸缘 (搭在盒体顶边内侧, 用于定位)
      // 凸缘顶面与盖板底面齐平, 向下插入盒体开口
      translate([WALL + RIM_T, WALL + RIM_T, -TONGUE_H])
        cube([TONGUE_L, TONGUE_W, TONGUE_H]);

      // 凸起文字 (左上角, 从盖板顶面向外凸出)
      //   offset(r=MARK_WEIGHT) 把字形轮廓整体外扩 -> 伪粗体 (OpenSCAD text 无 bold 选项)
      //   底部下探 0.2mm 与盖板重合, 避免共面布尔瑕疵
      translate([MARK_MARGIN, OUTER_W - MARK_MARGIN, LID_T - 0.2])
        linear_extrude(height=MARK_DEPTH + 0.2, convexity=20)
          offset(r=MARK_WEIGHT)
            text(MARK_TEXT1, size=MARK_SIZE, halign="left", valign="top");
      translate([MARK_MARGIN, OUTER_W - MARK_MARGIN - MARK_SIZE - MARK_GAP, LID_T - 0.2])
        linear_extrude(height=MARK_DEPTH + 0.2, convexity=20)
          offset(r=MARK_WEIGHT)
            text(MARK_TEXT2, size=MARK_SIZE, font=MARK_FONT, halign="left", valign="top");
    }

    // 开缝 (贯穿盖板 + 凸缘)
    translate([WALL + SLOT_X1, WALL + SLOT_Y1, -TONGUE_H - 1])
      cube([SLOT_CUT_L, SLOT_CUT_W, LID_T + TONGUE_H + 2]);
  }
}

// =====================================================
// 渲染
// =====================================================

if (PART == "box") {
  box();
} else if (PART == "lid") {
  lid();
} else if (PART == "exploded") {
  box();
  translate([0, 0, OUTER_H + 10])
    lid();
} else {
  // 完整装配
  box();
  translate([0, 0, OUTER_H])
    lid();
}
