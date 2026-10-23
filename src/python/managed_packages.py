import importlib
import importlib.abc
import importlib.machinery
import importlib.util
from pathlib import Path
import sys
import threading


class FreshSourceLoader(importlib.machinery.SourceFileLoader):
    def get_code(self, fullname):
        return self.source_to_code(self.get_data(self.path), self.path)


class ManagedPackages(importlib.abc.MetaPathFinder):
    def __init__(self, identity):
        self.identity = identity
        self.generation = 0
        self.roots = {}
        self.packages = {}
        self._thread_local = threading.local()

    def _record_module(self, fullname):
        stack = getattr(self._thread_local, 'stack', None)
        if stack:
            tx = stack[-1]
            if fullname not in tx:
                tx.append(fullname)

    def find_spec(self, fullname, path=None, target=None):
        root_name = fullname.partition('.')[0]
        directory = self.roots.get(root_name)
        if directory is None:
            return None
        self._record_module(fullname)
        if fullname == root_name:
            source = directory / '__init__.py'
            if source.is_file():
                source.resolve(strict=True).relative_to(directory)
                return importlib.util.spec_from_file_location(
                    fullname, source, loader=FreshSourceLoader(fullname, str(source)),
                    submodule_search_locations=[str(directory)])
            spec = importlib.machinery.ModuleSpec(fullname, loader=None, is_package=True)
            spec.submodule_search_locations = [str(directory)]
            return spec
        for search_path in path or ():
            Path(search_path).resolve(strict=True).relative_to(directory)
        spec = importlib.machinery.PathFinder.find_spec(fullname, path)
        if spec is None:
            raise ModuleNotFoundError('Managed Python module is missing: ' + fullname)
        for search_path in spec.submodule_search_locations or ():
            Path(search_path).resolve(strict=True).relative_to(directory)
        if spec.origin is not None:
            source = Path(spec.origin).resolve(strict=True)
            source.relative_to(directory)
            if isinstance(spec.loader, importlib.machinery.SourceFileLoader):
                spec.loader = FreshSourceLoader(fullname, str(source))
            elif not isinstance(spec.loader, importlib.machinery.ExtensionFileLoader):
                raise ImportError('Managed module requires source or a native extension: ' + fullname)
        return spec

    def clear(self):
        namespaces = tuple(self.roots)
        directories = tuple(self.roots.values())
        for name in tuple(sys.modules):
            if any(name == root or name.startswith(root + '.') for root in namespaces):
                del sys.modules[name]
        for search_path in tuple(sys.path_importer_cache):
            if any(Path(search_path).resolve().is_relative_to(root) for root in directories):
                del sys.path_importer_cache[search_path]
        self.roots.clear()
        self.packages.clear()
        self.generation += 1

    def load(self, origin, directory, script, symbol):
        directory = Path(directory).resolve(strict=True)
        entry = (directory / script).resolve(strict=True)
        relative = entry.relative_to(directory)
        if entry.suffix != '.py':
            raise ImportError('Managed entry must be a Python source file')
        parts = list(relative.with_suffix('').parts)
        if parts[-1] == '__init__':
            parts.pop()
        if any(not part.isidentifier() for part in parts):
            raise ImportError('Managed entry path components must be Python identifiers')
        key = (origin, directory, self.generation)
        root = self.packages.get(key)
        if root is None:
            root = f'_visionrt_package_{self.identity}_{self.generation}_{len(self.packages)}'
            self.packages[key] = root
            self.roots[root] = directory

        if not hasattr(self._thread_local, 'stack'):
            self._thread_local.stack = []
        tx = []
        self._thread_local.stack.append(tx)
        try:
            module = importlib.import_module('.'.join([root] + parts))
            result = getattr(module, symbol)
            self._thread_local.stack.pop()
            return result
        except BaseException:
            self._thread_local.stack.pop()
            for name in reversed(tx):
                if any(m.startswith(name + '.') for m in sys.modules):
                    continue
                if '.' in name:
                    parent_name, attr_name = name.rsplit('.', 1)
                    parent_mod = sys.modules.get(parent_name)
                    if parent_mod is not None and hasattr(parent_mod, attr_name):
                        try:
                            delattr(parent_mod, attr_name)
                        except (AttributeError, TypeError):
                            pass
                sys.modules.pop(name, None)
            raise
