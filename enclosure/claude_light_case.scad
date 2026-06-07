// =====================================================
// ClaudeLight 外壳 v3 - 底托 + 顶板, 红绿灯裸露
//
// 两件拼合:
//   - 底托 (tray): 敞口, ESP-32E 从上方放入
//   - 顶板 (cap): 覆盖底托, 开插槽让红绿灯竖直露出
//   - M2 螺丝从底面固定
//
// 打印建议:
//   - 底托: 敞口朝上打印, 无需支撑
//   - 顶板: 平面朝下打印, 无需支撑
//   - 材料: PLA/PETG, 0.2mm 层高, 3 回墙
// =====================================================

// ============== 可调参数 ==============

// ESP-32E 开发板
ESP_LEN     = 55;     // 长度 (USB-C 端到另一端)
ESP_WID     = 28;     // 宽度
ESP_PCB_H   = 1.6;    // PCB 厚度
ESP_MOD_H   = 10;     // 射频模块凸起
ESP_USB_W   = 9.5;    // USB-C 口宽度
ESP_USB_H   = 3.2;    // USB-C 口高度
ESP_USB_OFF = 0;      // USB-C 口中心 Y 偏移 (0=居中)

// 红绿灯模块
TL_LEN      = 75;     // PCB 总长
TL_WID      = 14;     // PCB 宽度
TL_PCB_H    = 1.6;    // PCB 厚度
TL_INSERT   = 8;      // 插入底座内的深度

// 外壳通用
WALL        = 1.2;    // 壁厚 (已减薄)
TOL         = 0.3;    // 间隙余量 (单侧)
CORNER_R    = 0;      // 圆角半径 (0=直角)

// 底托高度 (直接控制总高)
TRAY_TARGET_H = 30;   // 目标底托总高 (mm)

// ============== 计算值 ==============

INNER_L = ESP_LEN + 2*TOL;
INNER_W = ESP_WID + 2*TOL;
INNER_H = ESP_PCB_H + ESP_MOD_H + TOL + 1;  // 内腔高度

OUTER_L = INNER_L + 2*WALL;
OUTER_W = INNER_W + 2*WALL;

// 底托高度: 取内腔需求和目标高度中较大值
TRAY_H  = max(TRAY_TARGET_H, INNER_H + WALL);

// 顶板
CAP_TOP = 3;       // 插口实心厚度 (红绿灯夹持区域, 建议 ≥3mm)
CAP_H   = CAP_TOP;  // 顶板就是纯平板, 无空腔

// 四角支撑圆柱
CORNER_PAD_R  = 2;    // 1/4 圆柱半径
CORNER_PAD_H  = 3.5;  // 支撑面高度 (PCB 落在此高度上)

// 插槽
SLOT_W  = TL_WID + 2*TOL;
SLOT_D  = 3.3;        // 插槽缝隙宽度 (PCB+焊点厚度, 建议 ≥3mm)
SLOT_X  = OUTER_L / 2 - SLOT_W / 2;

// 螺丝位置 - USB 侧两个靠追拹左移避开按钮
USB_SIDE_MARGIN = 8;   // USB 侧螺丝柱 Y 方向偏移远离 USB 口
SCREW_MARGIN = 5.5;
SCREW_POSITIONS = [
  // USB 侧 (左端): 两个柱向中间偶移, 避开 USB 口旁的按钮
  [SCREW_MARGIN,            WALL + USB_SIDE_MARGIN],
  [SCREW_MARGIN,            OUTER_W - WALL - USB_SIDE_MARGIN],
  // 右端: 正常四角
  [OUTER_L - SCREW_MARGIN,  SCREW_MARGIN],
  [OUTER_L - SCREW_MARGIN,  OUTER_W - SCREW_MARGIN],
];

$fn = 32;

// ============== 渲染控制 ==============
// "all" = 装配, "tray" = 底托, "cap" = 顶板, "exploded" = 分解
PART = "tray";

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
// 底托 (敞口盒子)
// =====================================================

module tray_body() {
  difference() {
    rounded_box(OUTER_L, OUTER_W, TRAY_H, CORNER_R);
    // 顶面敞口 - 掏空内腔
    translate([WALL, WALL, WALL])
      cube([INNER_L, INNER_W, TRAY_H]);
  }
}

module tray_usb_cutout() {
  usb_y = WALL + ESP_WID/2 + ESP_USB_OFF - ESP_USB_W/2;
  // USB-C 开口 (前端面)
  translate([-1, usb_y, WALL + ESP_PCB_H])
    cube([WALL+2, ESP_USB_W, ESP_USB_H]);
  // 上沿倒角 (方便插线)
  translate([-1, usb_y - 1.5, WALL + ESP_PCB_H + ESP_USB_H])
    cube([WALL+2, ESP_USB_W + 3, 1.5]);
}

// 底部四角贴墙 1/4 圆柱支撑 PCB
module tray_corner_pads() {
  r = CORNER_PAD_R;
  h = CORNER_PAD_H;

  // 四角位置和对应的旋转角度
  corners = [
    [WALL,          WALL,          0],    // 左前角: 0°
    [OUTER_L-WALL,  WALL,          90],   // 右前角: 90°
    [WALL,          OUTER_W-WALL,  270],  // 左后角: 270°
    [OUTER_L-WALL,  OUTER_W-WALL,  180],  // 右后角: 180°
  ];

  for (c = corners) {
    translate([c[0], c[1], WALL])
      rotate([0, 0, c[2]])
        intersection() {
          cylinder(r=r, h=h);
          cube([r, r, h]);
        }
  }
}

module tray_standoffs() {
  // PCB 支撑 + 螺丝柱 (筒径减小, 避开按钮)
  hole_r     = 1.1;   // M2 过孔
  boss_r     = 2.5;   // 柱外径 (已减小)
  boss_h     = TRAY_H - WALL - 2;  // 柱高 (适配底托高度)
  step_r     = 1.8;   // 台阶半径
  step_h     = 3.5;   // 台阶高度 (PCB 安装面)

  for (pos = SCREW_POSITIONS) {
    translate([pos[0], pos[1], WALL])
      difference() {
        cylinder(r=boss_r, h=boss_h);
        translate([0, 0, -1])
          cylinder(r=hole_r, h=boss_h + 2);
        translate([0, 0, step_h])
          cylinder(r=boss_r + 1, h=boss_h);
      }
    // 台阶环
    translate([pos[0], pos[1], WALL])
      difference() {
        cylinder(r=step_r, h=step_h);
        cylinder(r=hole_r, h=step_h + 1);
      }
  }
}

module tray_screw_holes() {
  // M2 螺丝过孔 + 沉头 (从底面拧入)
  hole_r = 1.1;
  sink_r = 2.2;
  for (pos = SCREW_POSITIONS) {
    translate([pos[0], pos[1], -1]) {
      cylinder(r=sink_r, h=WALL * 0.6);   // 沉头
      cylinder(r=hole_r, h=TRAY_H + 2);    // 过孔贯穿到底托顶面
    }
  }
}



module tray_vent_slots() {
  // 底板散热槽
  slot_w = 2;
  n_slots = floor(INNER_L / 6);
  for (i = [0 : n_slots - 1]) {
    x = WALL + 4 + i * (INNER_L - 8) / max(n_slots - 1, 1);
    translate([x, WALL + 4, -1])
      rounded_box(slot_w, INNER_W - 8, WALL + 2, 0.5);
  }
}



module tray() {
  difference() {
    union() {
      tray_body();
      tray_corner_pads();
    }
    tray_usb_cutout();
    tray_vent_slots();
  }
}

// =====================================================
// 顶板 (平板 + 插槽 + 弹片)
// =====================================================

module cap_body() {
  // 纯实心平板, 无空腔, 打印友好
  rounded_box(OUTER_L, OUTER_W, CAP_H, CORNER_R);
}

module cap_slot() {
  // 红绿灯 PCB 插槽 (贯穿顶板)
  translate([SLOT_X, OUTER_W/2 - SLOT_D/2, -1])
    cube([SLOT_W, SLOT_D, CAP_H + 2]);

  // 插槽两端圆角 (PCB 滑入引导)
  for (dx = [0, SLOT_W - 2*TOL]) {
    translate([SLOT_X + dx, OUTER_W/2, -1])
      cylinder(r=SLOT_D/2 + 0.3, h=CAP_H + 2);
  }
}

module cap_clips() {
  // PCB 弹片 - 从平板底面向下延伸的两条薄肋, 夹住 PCB
  rib_w = 1.0;
  rib_h = TL_INSERT - 1;  // 向下伸入底托内的长度
  gap   = SLOT_D;  // 跟随插槽宽度

  for (sign = [-1, 1]) {
    y_pos = OUTER_W/2 + sign * (gap/2 + rib_w/2);
    translate([SLOT_X - 1, y_pos - rib_w/2, -rib_h])
      cube([SLOT_W + 2, rib_w, rib_h]);
  }
}

module cap_screw_holes() {
  hole_r = 1.1;
  sink_r = 2;
  for (pos = SCREW_POSITIONS) {
    translate([pos[0], pos[1], -1]) {
      cylinder(r=hole_r, h=CAP_H + 2);  // 过孔
    }
    // 沉头在顶面
    translate([pos[0], pos[1], CAP_H - WALL * 0.8])
      cylinder(r=sink_r, h=WALL);
  }
}



module cap() {
  difference() {
    union() {
      cap_body();
      cap_clips();
    }
    cap_slot();
  }
}

// =====================================================
// 调试: 板子位置 (半透明)
// =====================================================

module debug_pcbs() {
  %color("gray", 0.4)
    translate([WALL + TOL, WALL + TOL, WALL + 3.5])
      cube([ESP_LEN, ESP_WID, ESP_PCB_H]);

  %color("green", 0.5)
    translate([SLOT_X + TOL, OUTER_W/2 - TL_PCB_H/2, TRAY_H + CAP_H - TL_INSERT])
      cube([TL_WID, TL_PCB_H, TL_LEN]);
}

// =====================================================
// 渲染
// =====================================================

if (PART == "tray") {
  tray();
}
else if (PART == "cap") {
  cap();
}
else if (PART == "exploded") {
  // 底托在下面
  tray();
  // 顶板向上分离
  translate([0, 0, TRAY_H + 15])
    cap();
  debug_pcbs();
}
else {
  // 完整装配
  tray();
  translate([0, 0, TRAY_H])
    cap();
  debug_pcbs();
}
