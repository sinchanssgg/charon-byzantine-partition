package charon;

simple CharonNodeModule {
    parameters:
        int nodeId;
        int t;
        int c;
        int deltaMax;
        bool byzantine = default(false);
        int byzBehavior = default(0);  // 0=silent,1=selfwd,2=stale,3=flood
        @display("i=block/host");
    gates:
        inout port[];
}

network CharonRing {
    parameters:
        int numNodes = default(10);
        int t        = default(1);
        int c        = default(1);
        int deltaMax = default(5);
    submodules:
        node[numNodes]: CharonNodeModule {
            nodeId    = index;
            t         = parent.t;
            c         = parent.c;
            deltaMax  = parent.deltaMax;
        }
    connections:
        for i=0..numNodes-2 {
            node[i].port++ <--> node[i+1].port++;
        }
        node[numNodes-1].port++ <--> node[0].port++;
}
