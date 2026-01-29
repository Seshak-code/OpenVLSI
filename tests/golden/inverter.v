module inverter (
    input in, output out, inout VDD, inout VSS
);

    // Internal Nets
    wire n1;
    wire n2;
    wire n3;

    pfet_01v8 M2 (.D(n2), .G(n1), .S(n3), .B(n3));
    nfet_01v8 M1 (.D(n2), .G(n1), .S(VSS), .B(VSS));
endmodule
