import bgpyc

a = bgpyc.AS(64512)
print(a, bgpyc.call_step(a), bgpyc.call_step(a))
a.method_a()
a.method_b("hello")
a.method_c()
print("bump:", a.bump(5))
print("asn:", a.get_asn(), "counter:", a.get_counter())

class MyAS(bgpyc.AS):
    def step(self):
        return 999

b = MyAS(65000)
print(b, bgpyc.call_step(b))  # should print 999 (override)
