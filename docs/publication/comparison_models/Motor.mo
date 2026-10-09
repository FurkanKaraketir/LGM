within LGMBenchmarks.Motor;
  "PM DC motor — parameter-aligned with Examples/Motor.lgm and LGM golden benchmarks"
  model Motor
    import Modelica.Units.SI;
    parameter SI.Resistance R = 1 "Armature resistance (i_R)";
    parameter SI.Inductance L = 1 "Armature inductance (i_L)";
    parameter SI.Inertia J = 1 "Rotor inertia (T_J)";
    parameter SI.RotationalDampingConstant B = 1 "Viscous damping (T_B)";
    parameter Real TF = 1 "Electromechanical coupling (transformer modulus)";
    Modelica.Electrical.Analog.Basic.Ground g;
    Modelica.Electrical.Analog.Sources.SignalVoltage vs(p, n)
      "Vs1 — use u.v = Vs1(t) for symbolic input";
    Modelica.Electrical.Analog.Basic.Resistor armature(R = R);
    Modelica.Electrical.Analog.Basic.Inductor winding(L = L);
    Modelica.Electrical.Analog.Basic.RotationalEMF emf(k = TF)
      "Back-EMF / torque coupling via TF";
    Modelica.Mechanics.Rotational.Components.Inertia rotor(J = J);
    Modelica.Mechanics.Rotational.Components.Damper damper(d = B);
    Modelica.Mechanics.Rotational.Components.Fixed fixed;
  equation
    connect(vs.n, g.p);
    connect(vs.p, armature.p);
    connect(armature.n, winding.p);
    connect(winding.n, emf.p);
    connect(emf.n, g.p);
    connect(emf.flange, rotor.flange_a);
    connect(rotor.flange_b, damper.flange_a);
    connect(damper.flange_b, fixed.flange);
    // Expected linear state vector (after stateSelect / linearize):
    //   x = {rotor.w, winding.i}  ↔  LGM {OmegaJ, i_L}
    //   u = vs.v
    // Match LGM golden: OmegaJ_dot = -B*OmegaJ/J + TF*i_L/J
    //                    i_L_dot   = Vs1/L - R*i_L/L - TF*OmegaJ/L
  end Motor;
