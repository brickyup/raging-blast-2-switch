  // Single Joy-Con held sideways (one per player in local versus): HID reports it as if held upright. Its four
  // buttons become Switch letters by position (right A, bottom B, top X, left Y), SL/SR the shoulders, its one
  // menu button Start, and its stick is turned to match.
  const uint32_t style = padGetStyleSet(&slot.pad);
  const bool single = !slot.pad.active_handheld && !(style & (HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadFullKey));
  const bool joy_left = single && (style & HidNpadStyleTag_NpadJoyLeft);
  const bool joy_right = single && (style & HidNpadStyleTag_NpadJoyRight);
  if (joy_left || joy_right) {
    uint64_t out = held & (HidNpadButton_StickL | HidNpadButton_StickR | HidNpadButton_ZL | HidNpadButton_ZR);
    auto map = [&](uint64_t from, uint64_t to) {
      if (held & from) out |= to;
    };
    HidAnalogStickState stick = joy_left ? left : right;
    if (joy_left) {  // turned counter-clockwise: stick on the left
      map(HidNpadButton_Down, HidNpadButton_A);
      map(HidNpadButton_Left, HidNpadButton_B);
      map(HidNpadButton_Right, HidNpadButton_X);
      map(HidNpadButton_Up, HidNpadButton_Y);
      map(HidNpadButton_LeftSL, HidNpadButton_L);
      map(HidNpadButton_LeftSR, HidNpadButton_R);
      map(HidNpadButton_Minus, HidNpadButton_Plus);
      if (REXCVAR_GET(input_joycon_rotate_stick)) stick = {-stick.y, stick.x};
    } else {  // turned clockwise: stick on the right
      map(HidNpadButton_X, HidNpadButton_A);
      map(HidNpadButton_A, HidNpadButton_B);
      map(HidNpadButton_Y, HidNpadButton_X);
      map(HidNpadButton_B, HidNpadButton_Y);
      map(HidNpadButton_RightSL, HidNpadButton_L);
      map(HidNpadButton_RightSR, HidNpadButton_R);
      map(HidNpadButton_Plus, HidNpadButton_Plus);
      if (REXCVAR_GET(input_joycon_rotate_stick)) stick = {stick.y, -stick.x};
    }
    held = out;
    left = stick;
    right = {};
  }
