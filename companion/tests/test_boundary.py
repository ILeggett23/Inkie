"""Synthetic temporary Git repositories verify the publication audit itself."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('boundary',Path(__file__).resolve().parents[1]/'scripts/audit_boundary.py')
boundary=importlib.util.module_from_spec(spec);spec.loader.exec_module(boundary)

class BoundaryRegression(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
        self.git('init','-q');self.git('config','user.email','synthetic@example.invalid');self.git('config','user.name','Synthetic Audit')
        (self.root/'web').mkdir();(self.root/'web'/'app.js').write_text('const synthetic = true;\n')
    def tearDown(self):self.tmp.cleanup()
    def git(self,*args):
        return subprocess.run(['git',*args],cwd=self.root,check=True,capture_output=True,text=True).stdout.strip()
    def commit(self):self.git('add','--','web');self.git('commit','-qm','Synthetic audit fixture')
    def test_empty_index_and_no_history_cannot_pass(self):
        self.assertTrue(boundary.audit([],self.root));self.assertTrue(boundary.history_audit(self.root,[]))
    def test_regular_companion_source_passes_current_and_history(self):
        self.commit();self.assertEqual(boundary.audit(['web/app.js'],self.root),[]);self.assertEqual(boundary.history_audit(self.root,['web/app.js']),[])
    def test_current_symlink_and_ancestor_are_rejected(self):
        file=self.root/'web'/'app.js';file.unlink();file.symlink_to('../README.md')
        self.assertIn('symlink',boundary.audit(['web/app.js'],self.root)[0])
        file.unlink();(self.root/'web').rmdir();(self.root/'external').mkdir();(self.root/'external'/'app.js').write_text('synthetic')
        (self.root/'web').symlink_to('external',target_is_directory=True)
        self.assertIn('symlink',boundary.audit(['web/app.js'],self.root)[0])
    def test_removed_historical_symlink_mode_still_rejected(self):
        file=self.root/'web'/'app.js';file.unlink();file.symlink_to('../synthetic-target');self.commit()
        file.unlink();file.write_text('const synthetic = true;');self.commit()
        self.assertEqual(boundary.audit(['web/app.js'],self.root),[])
        self.assertTrue(any('historical symlink' in x for x in boundary.history_audit(self.root,['web/app.js'])))
    def test_historical_gitlink_mode_is_rejected(self):
        self.commit();head=self.git('rev-parse','HEAD')
        self.git('update-index','--add','--cacheinfo','160000,'+head+',web/synthetic-module');self.git('commit','-qm','Synthetic gitlink')
        self.assertTrue(any('submodule' in x for x in boundary.history_audit(self.root,['web/app.js','web/synthetic-module'])))
    def test_secret_pattern_in_old_blob_is_not_hidden_by_clean_current_file(self):
        file=self.root/'web'/'app.js';file.write_text('const synthetic = "'+('sk'+'-'+'A'*32)+'";');self.commit()
        file.write_text('const synthetic = true;');self.commit()
        self.assertTrue(any('sensitive history' in x for x in boundary.history_audit(self.root,['web/app.js'])))
    def test_binary_and_historical_removed_file_are_rejected(self):
        (self.root/'web'/'bad.txt').write_bytes(b'\xff\x00');self.commit()
        self.assertTrue(boundary.audit(['web/bad.txt'],self.root))
        self.assertTrue(any('binary history' in x or 'outside audited' in x for x in boundary.history_audit(self.root,['web/app.js'])))

if __name__=='__main__':unittest.main()
