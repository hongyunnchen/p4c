control c(inout bit<32> x) {
    @name("a") action a_0(in bit<32> arg) {
        x = arg;
    }
    apply {
        a_0(32w15);
    }
}

control proto(inout bit<32> arg);
package top(proto p);
top(c()) main;
