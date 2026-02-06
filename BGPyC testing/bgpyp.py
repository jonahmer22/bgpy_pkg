class AS:
    def __init__(self, asn: int):
        self.asn = int(asn)
        self.counter = 0

    def step(self) -> int:
        self.counter += 1
        return self.counter

    def method_a(self) -> None:
        print("in method A")

    def method_b(self, msg: str) -> None:
        print(f"in method B: {msg}")

    def method_c(self) -> None:
        print("in method C")

    def bump(self, delta: int) -> int:
        self.counter += int(delta)
        return self.counter

    def reset(self) -> None:
        self.counter = 0

    def get_asn(self) -> int:
        return self.asn

    def get_counter(self) -> int:
        return self.counter

    def __repr__(self) -> str:
        return f"<bgpyp.AS asn={self.asn} counter={self.counter}>"


def call_step(obj):
    if type(obj) is AS:
        return AS.step(obj)
    return obj.step()
