// =====================================================
// ESP32 SuperMini 外壳
//
// 结构:
//   - 主体: 顶部开口盒子, 顶部内壁预留阴角用于扣盖
//   - 盖子: 板 + 凸舌, 凸舌插入阴角定位
//
// 打印建议:
//   - 主体: 敞口朝上打印, 无需支撑
//   - 盖子: 平面朝下打印, 无需支撑
//   - 材料: PLA/PETG, 0.2mm 层高, 3 回墙
// =====================================================

// ============== 可调参数 ==============

// 内腔尺寸 (盒内)
INNER_L = 48;    // 内腔长度 (mm)
INNER_W = 36;    // 内腔宽度 (mm), USB 开口所在侧
INNER_H = 23;    // 内腔高度 (mm)

// 壁厚
WALL = 1.5;      // 壁厚 (mm)

// USB-C 开口 (从盒内测量, 位于 X=0 窄板即 INNER_W=36mm 侧)
//   "水平" 指面向该侧板时的左右方向 (对应 Y 轴)
//   "垂直" 指面向该侧板时的上下方向 (对应 Z 轴)
USB_H1 = 9;      // 水平起始位置 (mm)
USB_H2 = 20;     // 水平结束位置 (mm)
USB_V1 = 12.5;   // 垂直起始位置, 从底面 (mm)
USB_V2 = 17.5;   // 垂直结束位置, 从底面 (mm)

// 内阴角 (顶部扣盖台阶)
LIP_H = 1.5;     // 阴角高度 (mm)
LIP_D = 0.8;     // 阴角深度, 内壁后退量 (mm)

// 盖子
LID_T = 1.5;     // 盖板厚度 (mm)

// 盖子开缝 (从盒内测量)
SLOT_X1 = 26;    // 缝隙 X 起始 (mm)
SLOT_Y1 = 6;     // 缝隙 Y 起始 (mm)
SLOT_Y2 = 11;    // 缝隙 Y 结束 (mm)

// 间隙余量
TOL = 0.2;       // 单侧间隙 (mm)

// 圆角
CORNER_R = 2;    // 圆角半径 (0 = 直角)

// ============== 计算值 ==============

OUTER_L = INNER_L + 2 * WALL;
OUTER_W = INNER_W + 2 * WALL;
OUTER_H = INNER_H + WALL;  // 底板壁厚 + 内腔

// USB 开口尺寸 (由参数联动)
USB_CUT_HW = USB_H2 - USB_H1;       // 开口水平宽度
USB_CUT_VH = USB_V2 - USB_V1;       // 开口垂直高度

// 阴角台阶内尺寸 (由参数联动)
LIP_INNER_L = INNER_L + 2 * LIP_D; // 阴角处内腔长度
LIP_INNER_W = INNER_W + 2 * LIP_D; // 阴角处内腔宽度

// 盖子凸舌尺寸 (由参数联动)
TONGUE_L = LIP_INNER_L - 2 * TOL;  // 凸舌长度
TONGUE_W = LIP_INNER_W - 2 * TOL;  // 凸舌宽度
TONGUE_H = LIP_H - TOL;            // 凸舌高度

// 盖子开缝尺寸 (由参数联动)
SLOT_X2 = OUTER_L;                  // 缝隙延伸到盖子右边缘
SLOT_CUT_L = SLOT_X2 - (WALL + SLOT_X1);  // 缝隙长度
SLOT_CUT_W = SLOT_Y2 - SLOT_Y1;    // 缝隙宽度

$fn = 32;

// ============== 渲染控制 ==============
// "box" = 仅主体, "lid" = 仅盖子, "all" = 装配, "exploded" = 分解
PART = "lid";

// =====================================================
// 公共: 圆角矩形
// =====================================================

module rounded_box(l, w, h, r) {
  if (r <= 0) {
    cube([l, w, h]);
  } else {
    hull() {
      translate([r, r, 0])       cylinder(r=r, h=h);
      translate([l-r, r, 0])     cylinder(r=r, h=h);
      translate([r, w-r, 0])     cylinder(r=r, h=h);
      translate([l-r, w-r, 0])   cylinder(r=r, h=h);
    }
  }
}

// =====================================================
// 主体 (顶部开口盒子 + 内阴角)
// =====================================================

module box_body() {
  difference() {
    // 外壳
    rounded_box(OUTER_L, OUTER_W, OUTER_H, CORNER_R);

    // 主内腔 (从底板顶面到盒子顶部)
    translate([WALL, WALL, WALL])
      cube([INNER_L, INNER_W, INNER_H + 1]);

    // 顶部内阴角 - 内壁后退 LIP_D 形成台阶
    translate([WALL - LIP_D, WALL - LIP_D, WALL + INNER_H - LIP_H])
      cube([LIP_INNER_L, LIP_INNER_W, LIP_H + 1]);
  }
}

module box_usb_cutout() {
  // USB-C 开口 (X=0 侧板, 即 INNER_W=36mm 窄板)
  //   水平方向 → Y 轴, 垂直方向 → Z 轴, 切穿 X 方向壁厚
  translate([-1, WALL + USB_H1, WALL + USB_V1])
    cube([WALL + 2, USB_CUT_HW, USB_CUT_VH]);
}

module box() {
  difference() {
    box_body();
    box_usb_cutout();
  }
}

// =====================================================
// 盖子 (盖板 + 凸舌)
// =====================================================

module lid() {
  difference() {
    union() {
      // 盖板 (覆盖主体顶部)
      rounded_box(OUTER_L, OUTER_W, LID_T, CORNER_R);

      // 凸舌 (插入阴角定位)
      translate([WALL - LIP_D + TOL, WALL - LIP_D + TOL, -TONGUE_H])
        cube([TONGUE_L, TONGUE_W, TONGUE_H]);
    }

    // 开缝 (贯穿盖板 + 凸舌)
    translate([WALL + SLOT_X1, WALL + SLOT_Y1, -TONGUE_H - 1])
      cube([SLOT_CUT_L, SLOT_CUT_W, LID_T + TONGUE_H + 2]);
  }
}

// =====================================================
// 渲染
// =====================================================

if (PART == "box") {
  box();
}
else if (PART == "lid") {
  lid();
}
else if (PART == "exploded") {
  box();
  translate([0, 0, OUTER_H + 10])
    lid();
}
else {
  // 完整装配
  box();
  translate([0, 0, OUTER_H])
    lid();
}
