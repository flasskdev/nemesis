"""Independent Python model and source-contract tests. Not a C++ build or a game test."""
import math
import random
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def direction(yaw):
    angle = math.radians(math.remainder(yaw, 360.0))
    return math.cos(angle), math.sin(angle)

def world(move, yaw):
    c, s = direction(yaw)
    f, l = move
    return c * f - s * l, s * f + c * l

def local(move, yaw):
    return world(move, -yaw)

def limit(move):
    scale = 1 / max(1, abs(move[0]), abs(move[1]))
    return move[0] * scale, move[1] * scale

def air_step(v, wish, dt=1/64, wishspeed=250, accel=12, cap=30, friction=1):
    n = math.hypot(*wish)
    if n < 0.0001:
        return v
    x, y = wish[0] / n, wish[1] / n
    add = min(wishspeed, cap) - v[0] * x - v[1] * y
    if add <= 0:
        return v
    step = min(add, max(0, wishspeed * accel * friction * dt))
    return v[0] + step * x, v[1] + step * y

def ideal(speed, dt=1/64, wishspeed=250, accel=12, cap=30, friction=1):
    if speed < 0.001:
        return 0
    projection = max(0, min(max(0, wishspeed), max(0, cap)) - max(0, wishspeed * accel * friction * dt))
    return math.degrees(math.acos(max(0, min(1, projection / speed))))

def choose(v, target_yaw, command_yaw, quantized=False, braking=False):
    speed = math.hypot(*v)
    if quantized:
        candidates = [(f, l) for f in (-1, 0, 1) for l in (-1, 0, 1) if f or l]
    elif braking:
        return local((-v[0]/speed, -v[1]/speed), command_yaw) if speed > 1 else (0, 0)
    elif speed < 15:
        return local(direction(target_yaw), command_yaw)
    else:
        vyaw = math.degrees(math.atan2(v[1], v[0]))
        theta = ideal(speed)
        candidates = [local(direction(vyaw + theta), command_yaw), local(direction(vyaw - theta), command_yaw)]
    target = direction(target_yaw)
    best, score = (0, 0), -math.inf
    for move in candidates:
        new = air_step(v, world(move, command_yaw))
        n = math.hypot(*new)
        heading = (new[0] * target[0] + new[1] * target[1]) / n if n > 0.001 else 1
        candidate_score = -n if braking else n - max(30, speed) * (1 - max(-1, min(1, heading)))
        if candidate_score > score + 0.00001:
            score, best = candidate_score, move
    return limit(best)

class MovementTests(unittest.TestCase):
    def test_basis_signs(self):
        self.assertEqual(world((0, 1), 0), (0, 1))
        f, l = local(world((1, 0), 0), 90)
        self.assertAlmostEqual(f, 0)
        self.assertAlmostEqual(l, -1)

    def test_rebase_preserves_world_vector(self):
        rng = random.Random(20260906)
        for _ in range(20000):
            a, b = rng.uniform(-720, 720), rng.uniform(-720, 720)
            v = direction(rng.uniform(-180, 180))
            magnitude = rng.random()
            command = v[0] * magnitude, v[1] * magnitude
            expected = world(command, a)
            actual = world(limit(local(expected, b)), b)
            for x, y in zip(expected, actual):
                self.assertAlmostEqual(x, y, places=10)

    def test_quantized_idempotent_and_bounded(self):
        rng = random.Random(7)
        for _ in range(5000):
            v = rng.uniform(-1000, 1000), rng.uniform(-1000, 1000)
            a, b = rng.uniform(-180, 180), rng.uniform(-180, 180)
            q = choose(v, a, b, True)
            self.assertEqual(q, choose(v, a, b, True))
            self.assertTrue(all(x in (-1, 0, 1) for x in q))
            normal = choose(v, a, b)
            self.assertTrue(all(math.isfinite(x) and abs(x) <= 1 for x in normal))

    def test_analog_solution_independent_of_command_yaw(self):
        for v in ((350, 0), (200, -210), (0, 0), (-100, 95)):
            for target in (-170, -35, 0, 80, 175):
                expected = world(choose(v, target, 0), 0)
                for yaw in range(-180, 181, 5):
                    actual = world(choose(v, target, yaw), yaw)
                    for x, y in zip(expected, actual):
                        self.assertAlmostEqual(x, y, places=9)

    def test_ideal_angle_against_brute_force(self):
        for speed in (15, 100, 350, 1000):
            for accel in (1, 12, 100):
                optimal = air_step((speed, 0), direction(ideal(speed, accel=accel)), accel=accel)
                expected = math.hypot(*optimal)
                for degree in range(-1800, 1801):
                    sample = air_step((speed, 0), direction(degree / 10), accel=accel)
                    self.assertGreaterEqual(expected + 1e-8, math.hypot(*sample))

    def test_zero_friction(self):
        self.assertEqual(air_step((350, 0), (0, 1), friction=0), (350, 0))

    def test_stable_subtick_order_and_delta(self):
        events = [(0.7, 'press'), (0, 'release'), (0, 'analog')]
        self.assertEqual(sorted(events, key=lambda e: e[0]), [(0, 'release'), (0, 'analog'), (0.7, 'press')])
        for old, final in (((0, 1), (1, 0)), ((-1, -1), (1, 1))):
            delta = final[0] - old[0], final[1] - old[1]
            self.assertEqual((old[0] + delta[0], old[1] + delta[1]), final)

    def test_spin_command_timing(self):
        def phase(commands):
            angle, last = 17.0, commands[0]
            for command in commands[1:]:
                angle = math.remainder(angle + (command - last) / 64 * 360, 360)
                last = command
            return angle
        self.assertEqual(phase([100, 101, 101, 102]), phase([100, 102]))
        self.assertEqual(phase([100, 164]), 17.0)

    def test_source_contracts(self):
        hook = (ROOT / 'core/hooks/impl/cheat.cpp').read_text()
        strafer = (ROOT / 'core/features/movement/impl/airstrafe.cpp').read_text()
        legacy = (ROOT / 'core/features/movement/impl/test_strafer.cpp').read_text()
        inputs = (ROOT / 'core/systems/impl/input.cpp').read_text()
        self.assertNotIn('final_base->set_forwardmove( 0.0f )', hook)
        self.assertNotIn('set_view_angles(', strafer)
        self.assertNotIn('set_yaw_delta(', strafer + legacy)
        self.assertNotIn('has_move_subticks', inputs)
        self.assertIn('g_airstrafe.store_angles()', hook)
        self.assertIn('if (!features::combat::g_rage.is_firing_this_tick())', hook)
        self.assertLess(hook.index('g_rage.on_create_move'), hook.index('g_misc.antiaim().on_create_move'))
        self.assertLess(hook.index('g_misc.antiaim().on_create_move'), hook.index('g_airstrafe.finalize'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
