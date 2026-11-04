"""tools/pso_merge.py 的回归测试：python3 -I tools/test_pso_merge.py | regression tests for tools/pso_merge.py"""
import sys, tempfile, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pso_merge


def rline(vs=b'0123456789abcdef_b', fs=b'fedcba9876543210_b_um32', rest=b''):
    raw = vs.ljust(24, b'\0') + fs.ljust(24, b'\0') + rest.ljust(432 - 48, b'\0')
    return 'R\t' + raw.hex()


class PsoMerge(unittest.TestCase):
    def setUp(self):
        self.d = Path(tempfile.mkdtemp())

    def put(self, name, lines):
        p = self.d / name; p.write_text('\n'.join(lines) + '\n', encoding='utf-8'); return p

    def test_formats(self):
        ok = ['S\t0123456789abcdef_b\t0', 'S\t0123456789abcdef_b_ia12\t4', 'C\t0123456789abcdef_b', rline(), rline(fs=b'')]
        bad = ['S\t0123456789abcdef_b\t9', 'S\tXYZ\t0', 'R\t' + 'ab' * 10, rline(vs=b'hello'), rline(vs=b'0123456789abcdef_b\0junk'), 'hello', 'S\t0123456789abcdef_b\t0\textra']
        good, wrong = pso_merge.read(self.put('u.tsv', ok + bad + ['# comment', '']))
        self.assertEqual(good, set(ok))
        self.assertEqual(len(wrong), len(bad))

    def test_add_is_sorted_dedup_and_idempotent(self):
        pso_merge.SHARED = self.d / 'pso' / 'shared.tsv'
        a = self.put('a.tsv', [rline(), 'S\t0123456789abcdef_b\t0'])
        b = self.put('b.tsv', ['S\t0123456789abcdef_b\t0', 'C\t1111111111111111_b'])
        pso_merge.main(['add', str(a), str(b)])
        first = pso_merge.SHARED.read_text(encoding='utf-8')
        pso_merge.main(['add', str(b)])
        self.assertEqual(first, pso_merge.SHARED.read_text(encoding='utf-8'))
        body = [l for l in first.splitlines() if not l.startswith('#')]
        self.assertEqual([l[0] for l in body], ['S', 'C', 'R'])
        self.assertEqual(pso_merge.main(['check', str(pso_merge.SHARED)]), 0)

    def test_seed_skips_missing_inputs(self):
        out = self.d / 'out' / 'seed.tsv'
        pso_merge.main(['seed', str(out), str(self.put('a.tsv', ['S\t0123456789abcdef_b\t0'])), str(self.d / 'none.tsv')])
        self.assertIn('S\t0123456789abcdef_b\t0', out.read_text(encoding='utf-8').splitlines())


if __name__ == '__main__':
    unittest.main()
