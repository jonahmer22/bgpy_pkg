import bgpyc

a = bgpyc.AS(64512)
print(a, bgpyc.call_step(a), bgpyc.call_step(a))

class MyAS(bgpyc.AS):
    def step(self):
        return 999

b = MyAS(65000)
print(b, bgpyc.call_step(b))  # should print 999 (override)