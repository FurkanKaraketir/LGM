within LGMBenchmarks.Electrical;
  "Coupled RLC — topology sketch aligned with Examples/Electrical.lgm"
  model Electrical
    import Modelica.Units.SI;
    parameter SI.Resistance R1 = 1, R2 = 1;
    parameter SI.Inductance L3 = 1, L7 = 1;
    parameter SI.Capacitance C4 = 1, C5 = 1;
    Modelica.Electrical.Analog.Basic.Ground g;
    Modelica.Electrical.Analog.Sources.SignalVoltage vs1 "Vs1 at V1–ground branch";
    Modelica.Electrical.Analog.Sources.SignalCurrent is2 "Is2 at V3";
    Modelica.Electrical.Analog.Basic.Resistor r1(R = R1), r2(R = R2);
    Modelica.Electrical.Analog.Basic.Inductor l3(L = L3), l7(L = L7);
    Modelica.Electrical.Analog.Basic.Capacitor c4(C = C4), c5(C = C5);
    Modelica.Electrical.Analog.Basic.Node n1, n2, n3, n4, n5;
  equation
    connect(g.p, n5.p);
    connect(vs1.n, n5.p);
    connect(vs1.p, n1.p);
    connect(r1.p, n1.p);
    connect(r1.n, n2.p);
    connect(c4.p, n2.p);
    connect(c4.n, n5.p);
    connect(c5.p, n1.p);
    connect(c5.n, n2.p);
    connect(l3.p, n5.p);
    connect(l3.n, n4.p);
    connect(r2.p, n3.p);
    connect(r2.n, n4.p);
    connect(l7.p, n3.p);
    connect(l7.n, n1.p);
    connect(is2.p, n3.p);
    connect(is2.n, g.p);
    // LGM state choice (synthetic across): x = {C5_across, i_L7}
    //   C5_across := v(n1) - v(n2)  (tautology across on i_C5)
    // Modelica default: node voltages or capacitor voltages — reorder to compare.
    // See cross_tool_comparison.md § Electrical.
  end Electrical;
