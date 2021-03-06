control c(out bit<32> x);
package top(c _c);
control my(out bit<32> x) {
    action act() {
        x = 32w2;
    }
    table tbl_act() {
        actions = {
            act();
        }
        const default_action = act();
    }
    apply {
        tbl_act.apply();
    }
}

top(my()) main;
