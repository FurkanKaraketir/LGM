within LGMBenchmarks.PumpAccumulator;
  "Multi-domain pump–accumulator — parameter reference for Examples/PumpAccumulator.lgm"
  model PumpAccumulator
    import Modelica.Units.SI;
    parameter SI.Resistance R = 1 "Electrical R";
    parameter SI.Inductance L = 1 "Electrical L";
    parameter Real K_a = 1 "Electrical–mechanical TF";
    parameter Real K_b = 1 "Fluid–mechanical TF";
    parameter SI.Mass M = 1 "Mechanical mass (f_M)";
    parameter SI.Resistance R_leak = 1;
    parameter SI.Inductance L_f = 1 "Fluid inertance";
    parameter SI.Resistance R_f = 1 "Fluid resistance";
    parameter Real A = 1 "Gyrator / area modulus";
    // LGM golden state equations (symbolic target for manual comparison):
    //   Q_L_f_dot = Fs2/(A*L_f) - R_f*Q_L_f/L_f - i_L/(K_a*K_b*L_f)
    //   i_L_dot   = Vs1/L - R*i_L/L + Q_L_f/(L*K_a*K_b)
    //   Vm_dot    = Fs2/M + A*Q_L_f*R_leak/M - A^2*Vm*R_leak/M
    // Full acausal hydraulic–electrical–mechanical network: see LGM graph.
  end PumpAccumulator;
