// Copyright 2018 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "tools/cpp/runfiles/runfiles.h"

#ifdef COMPILER_MSVC
#include <windows.h>
#else  // not COMPILER_MSVC
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // COMPILER_MSVC

#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#ifdef COMPILER_MSVC
#include <memory>
#endif  // COMPILER_MSVC

namespace bazel {
namespace tools {
namespace cpp {
namespace runfiles {

using std::function;
using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

bool starts_with(const string& s, const string& prefix) {
  if (prefix.empty()) {
    return true;
  }
  if (s.empty()) {
    return false;
  }
  return s.find(prefix) == 0;
}

bool contains(const string& s, const string& substr) {
  if (substr.empty()) {
    return true;
  }
  if (s.empty()) {
    return false;
  }
  return s.find(substr) != string::npos;
}

bool ends_with(const string& s, const string& suffix) {
  if (suffix.empty()) {
    return true;
  }
  if (s.empty()) {
    return false;
  }
  return s.rfind(suffix) == s.size() - suffix.size();
}

class RunfilesImpl : public Runfiles {
 public:
  static Runfiles* Create(const string& argv0,
                          function<string(const string&)> env_lookup,
                          string* error);

  string Rlocation(const string& path) const override;

  // Returns the runtime-location of a given runfile.
  //
  // This method assumes that the caller already validated the `path`. See
  // Runfiles::Rlocation for requirements.
  virtual string RlocationChecked(const string& path) const = 0;

  const vector<pair<string, string> >& EnvVars() const { return envvars_; }

 protected:
  RunfilesImpl(const vector<pair<string, string> >&& envvars)
      : envvars_(std::move(envvars)) {}
  virtual ~RunfilesImpl() {}

 private:
  const vector<pair<string, string> > envvars_;
};

// Runfiles implementation that parses a runfiles-manifest to look up runfiles.
class ManifestBased : public RunfilesImpl {
 public:
  // Returns a new `ManifestBased` instance.
  // Reads the file at `manifest_path` to build a map of the runfiles.
  // Returns nullptr upon failure.
  static ManifestBased* Create(const string& manifest_path,
                               const vector<pair<string, string> >&& envvars,
                               string* error);

  string RlocationChecked(const string& path) const override;

 private:
  ManifestBased(const string& manifest_path,
                const map<string, string>&& runfiles_map,
                const vector<pair<string, string> >&& envvars)
      : RunfilesImpl(std::move(envvars)),
        manifest_path_(manifest_path),
        runfiles_map_(std::move(runfiles_map)) {}

  ManifestBased(const ManifestBased&) = delete;
  ManifestBased(ManifestBased&&) = delete;
  ManifestBased& operator=(const ManifestBased&) = delete;
  ManifestBased& operator=(ManifestBased&&) = delete;

  static bool ParseManifest(const string& path, map<string, string>* result,
                            string* error);

  const string manifest_path_;
  const map<string, string> runfiles_map_;
};

// Runfiles implementation that appends runfiles paths to the runfiles root.
class DirectoryBased : public RunfilesImpl {
 public:
  DirectoryBased(string runfiles_path,
                 const vector<pair<string, string> >&& envvars)
      : RunfilesImpl(std::move(envvars)),
        runfiles_path_(std::move(runfiles_path)) {}
  string RlocationChecked(const string& path) const override;

 private:
  DirectoryBased(const DirectoryBased&) = delete;
  DirectoryBased(DirectoryBased&&) = delete;
  DirectoryBased& operator=(const DirectoryBased&) = delete;
  DirectoryBased& operator=(DirectoryBased&&) = delete;

  const string runfiles_path_;
};

bool IsReadableFile(const string& path) {
  return std::ifstream(path).is_open();
}

bool IsDirectory(const string& path) {
#ifdef COMPILER_MSVC
  DWORD attrs = GetFileAttributesA(path.c_str());
  return (attrs != INVALID_FILE_ATTRIBUTES) &&
         (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat buf;
  return stat(path.c_str(), &buf) == 0 && S_ISDIR(buf.st_mode);
#endif
}

Runfiles* RunfilesImpl::Create(const string& argv0,
                               function<string(const string&)> env_lookup,
                               string* error) {
  string manifest, directory;
  if (!Runfiles::PathsFrom(
          argv0, env_lookup("RUNFILES_MANIFEST_FILE"),
          env_lookup("RUNFILES_DIR"),
          [](const string& path) { return IsReadableFile(path); },
          [](const string& path) { return IsDirectory(path); }, &manifest,
          &directory)) {
    if (error) {
      std::ostringstream err;
      err << "ERROR: " << __FILE__ << "(" << __LINE__
          << "): cannot find runfiles (argv0=\"" << argv0 << "\")";
      *error = err.str();
    }
    return nullptr;
  }

  const vector<pair<string, string> > envvars = {
      {"RUNFILES_MANIFEST_FILE", manifest},
      {"RUNFILES_DIR", directory},
      // TODO(laszlocsomor): remove JAVA_RUNFILES once the Java launcher can
      // pick up RUNFILES_DIR.
      {"JAVA_RUNFILES", directory}};

  if (!manifest.empty()) {
    return ManifestBased::Create(manifest, std::move(envvars), error);
  } else {
    return new DirectoryBased(directory, std::move(envvars));
  }
}

bool IsAbsolute(const string& path) {
  if (path.empty()) {
    return false;
  }
  char c = path.front();
  return (c == '/' && (path.size() < 2 || path[1] != '/')) ||
         (path.size() >= 3 &&
          ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) &&
          path[1] == ':' && (path[2] == '\\' || path[2] == '/'));
}

string GetEnv(const string& key) {
#ifdef COMPILER_MSVC
  DWORD size = ::GetEnvironmentVariableA(key.c_str(), NULL, 0);
  if (size == 0) {
    return std::move(string());  // unset or empty envvar
  }
  std::unique_ptr<char[]> value(new char[size]);
  ::GetEnvironmentVariableA(key.c_str(), value.get(), size);
  return move(string(value.get()));
#else
  char* result = getenv(key.c_str());
  return std::move((result == NULL) ? string() : string(result));
#endif
}

string RunfilesImpl::Rlocation(const string& path) const {
  if (path.empty() || starts_with(path, "../") || contains(path, "/..") ||
      starts_with(path, "./") || contains(path, "/./") ||
      ends_with(path, "/.") || contains(path, "//")) {
    return std::move(string());
  }
  if (IsAbsolute(path)) {
    return path;
  }
  return RlocationChecked(path);
}

ManifestBased* ManifestBased::Create(
    const string& manifest_path, const vector<pair<string, string> >&& envvars,
    string* error) {
  map<string, string> runfiles;
  return ParseManifest(manifest_path, &runfiles, error)
             ? new ManifestBased(manifest_path, std::move(runfiles),
                                 std::move(envvars))
             : nullptr;
}

string ManifestBased::RlocationChecked(const string& path) const {
  const auto value = runfiles_map_.find(path);
  return std::move(value == runfiles_map_.end() ? string() : value->second);
}

bool ManifestBased::ParseManifest(const string& path,
                                  map<string, string>* result, string* error) {
  std::ifstream stm(path);
  if (!stm.is_open()) {
    if (error) {
      std::ostringstream err;
      err << "ERROR: " << __FILE__ << "(" << __LINE__
          << "): cannot open runfiles manifest \"" << path << "\"";
      *error = err.str();
    }
    return false;
  }
  string line;
  std::getline(stm, line);
  size_t line_count = 1;
  while (!line.empty()) {
    string::size_type idx = line.find_first_of(' ');
    if (idx == string::npos) {
      if (error) {
        std::ostringstream err;
        err << "ERROR: " << __FILE__ << "(" << __LINE__
            << "): bad runfiles manifest entry in \"" << path << "\" line #"
            << line_count << ": \"" << line << "\"";
        *error = err.str();
      }
      return false;
    }
    (*result)[line.substr(0, idx)] = line.substr(idx + 1);
    std::getline(stm, line);
    ++line_count;
  }
  return true;
}

string DirectoryBased::RlocationChecked(const string& path) const {
  return std::move(runfiles_path_ + "/" + path);
}

}  // namespace

namespace testing {

Runfiles* TestOnly_CreateRunfiles(const std::string& argv0,
                                  function<string(const string&)> env_lookup,
                                  string* error) {
  return RunfilesImpl::Create(argv0, env_lookup, error);
}

bool TestOnly_IsAbsolute(const string& path) { return IsAbsolute(path); }

}  // namespace testing

Runfiles* Runfiles::Create(const string& argv0, string* error) {
  return RunfilesImpl::Create(
      argv0,
      [](const string& key) {
        if (key == "RUNFILES_MANIFEST_FILE" || key == "RUNFILES_DIR") {
          string val(GetEnv(key));
          return std::move(val);
        } else {
          return std::move(string());
        }
      },
      error);
}

Runfiles* Runfiles::CreateManifestBased(const string& manifest_path,
                                        string* error) {
  return ManifestBased::Create(manifest_path,
                               {{"RUNFILES_MANIFEST_FILE", manifest_path},
                                {"RUNFILES_DIR", ""},
                                {"JAVA_RUNFILES", ""}},
                               error);
}

Runfiles* Runfiles::CreateDirectoryBased(const string& directory_path,
                                         string* error) {
  // Note: `error` is intentionally unused because we don't expect any errors
  // here. We expect an `error` pointer so that we may use it in the future if
  // need be, without having to change the API.
  return new DirectoryBased(directory_path,
                            {{"RUNFILES_MANIFEST_FILE", ""},
                             {"RUNFILES_DIR", directory_path},
                             {"JAVA_RUNFILES", directory_path}});
}

bool Runfiles::PathsFrom(const string& argv0, string mf, string dir,
                         function<bool(const string&)> is_runfiles_manifest,
                         function<bool(const string&)> is_runfiles_directory,
                         string* out_manifest, string* out_directory) {
  out_manifest->clear();
  out_directory->clear();

  bool mfValid = is_runfiles_manifest(mf);
  bool dirValid = is_runfiles_directory(dir);

  if (!mfValid && !dirValid) {
    mf = argv0 + ".runfiles/MANIFEST";
    dir = argv0 + ".runfiles";
    mfValid = is_runfiles_manifest(mf);
    dirValid = is_runfiles_directory(dir);
    if (!mfValid) {
      mf = argv0 + ".runfiles_manifest";
      mfValid = is_runfiles_manifest(mf);
    }
  }

  if (!mfValid && !dirValid) {
    return false;
  }

  if (!mfValid) {
    mf = dir + "/MANIFEST";
    mfValid = is_runfiles_manifest(mf);
    if (!mfValid) {
      mf = dir + "_manifest";
      mfValid = is_runfiles_manifest(mf);
    }
  }

  if (!dirValid) {
    static const size_t kSubstrLen = 9;  // "_manifest" or "/MANIFEST"
    dir = mf.substr(0, mf.size() - kSubstrLen);
    dirValid = is_runfiles_directory(dir);
  }

  if (mfValid) {
    *out_manifest = mf;
  }

  if (dirValid) {
    *out_directory = dir;
  }

  return true;
}

}  // namespace runfiles
}  // namespace cpp
}  // namespace tools
}  // namespace bazel
