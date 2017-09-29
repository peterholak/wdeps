#include "pe-parse/parse.h"
#include <iostream>
#include <set>
#include <vector>
#include <optional>
#include <windows.h>
#include <map>
#include <memory>
#include <functional>
#include <iomanip>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

using namespace peparse;
using namespace std;
namespace po = boost::program_options;
namespace fs = boost::filesystem;

int getImports(void *N, VA impAddr, string &modName, string &symName) {
    auto modules = reinterpret_cast<set<string>*>(N);
    modules->insert(modName);
    return 0;
}

vector<string> split(const string& s, char delimiter) {
    vector<string> result;
    if (s.empty()) {
        return result;
    }
    size_t previous = 0;
    size_t position;
    while ((position = s.find(delimiter, previous)) != string::npos) {
        result.push_back(s.substr(previous, position - previous));
        previous = position + 1;
    }
    result.push_back(s.substr(previous));
    return result;
}

vector<string> getPath() {
    // TODO: portable shit, make configurable for cross-system use
    string path = getenv("PATH");
    auto parts = split(path, ';');
    for (auto& s : parts) {
        if (s[s.length() - 1] == '/' || s[s.length() - 1] == '\\') {
            s.pop_back();
        }
    }
    return parts;
}

size_t getLastSeparator(const std::string& file) {
    auto lastSlash = file.find_last_of('/');
    auto lastBackslash = file.find_last_of('\\');

    if (lastSlash == string::npos && lastBackslash == string::npos) {
        return string::npos;
    }

    auto last = (
        lastSlash == string::npos ?
        lastBackslash :
        (lastBackslash == string::npos ? lastSlash : max(lastSlash, lastBackslash))
    );

    if (last == file.length() - 1) {
        return string::npos;
    }

    return last;
}

string getFileDirectory(const string& file) {
    auto last = getLastSeparator(file);
    if (last == string::npos) { return "."; }
    return file.substr(0, last);
}

string getFileName(const std::string& file) {
    auto last = getLastSeparator(file);
    if (last == string::npos) { return file; }
    return file.substr(last + 1);
}

string getFileNameWithCorrectCase(const std::string& existingFile) {
    // TODO: portable shit, also this is a hacky but quick way to get the file name with the correct case
    char shortBuffer[MAX_PATH];
    char longBuffer[MAX_PATH];
    // TODO: also check for failures
    GetShortPathNameA(existingFile.c_str(), shortBuffer, MAX_PATH);
    GetLongPathNameA(shortBuffer, longBuffer, MAX_PATH);
    return getFileName(longBuffer);
}

bool directoryContains(const string& directory, const string& file) {
    return fs::exists(directory + "/" + file);
}

string systemDirectory() {
    // TODO: portable shit
    char buffer[MAX_PATH];
    GetSystemDirectoryA(buffer, MAX_PATH);
    return string(buffer);
}

string windowsDirectory() {
    // TODO: portable shit
    char buffer[MAX_PATH];
    GetWindowsDirectoryA(buffer, MAX_PATH);
    return string(buffer);
}

struct DllPath {
    string path;
    enum { User, System, Missing } location;

    DllPath& fix() {
        path = getFileDirectory(path) + "/" + getFileNameWithCorrectCase(path);
        return *this;
    }
};

DllPath getDllPath(const string& appDirectory, const string& dllName) {
    if (directoryContains(appDirectory, dllName)) {
        return DllPath{ appDirectory + "/" + dllName, DllPath::User }.fix();
    }

    if (directoryContains(systemDirectory(), dllName)) {
        return DllPath{ systemDirectory() + "/" + dllName, DllPath::System }.fix();
    }

    if (directoryContains(windowsDirectory(), dllName)) {
        return DllPath{ windowsDirectory() + "/" + dllName, DllPath::System }.fix();
    }

    for (auto& s : getPath()) {
        if (directoryContains(s, dllName)) {
            return DllPath{ s + "/" + dllName, DllPath::User }.fix();
        }
    }

    return { dllName, DllPath::Missing };
}

struct Dll {
    explicit Dll(DllPath path) : path(move(path)) { }

    DllPath path;
    // This gets mutated during fillDependencies
    bool isValid = true;
    bool stripped = false;
    vector<Dll*> dependencies;

    void fillDependencies(map<string, unique_ptr<Dll>>& globalMap, bool recurseIntoSystem = false) {
        if (path.location == DllPath::Missing || (path.location == DllPath::System && !recurseIntoSystem)) {
            return;
        }
        auto directory = getFileDirectory(path.path);
        auto parsed = ParsePEFromFile(path.path.c_str());
        if (parsed == nullptr) {
            isValid = false;
            return;
        }

        set<string> modules;
        IterImpVAString(parsed, getImports, &modules);
        auto& c = parsed->peHeader.nt.FileHeader.Characteristics;
        if ((c & IMAGE_FILE_DEBUG_STRIPPED) && (c & IMAGE_FILE_LINE_NUMS_STRIPPED) && (c & IMAGE_FILE_LOCAL_SYMS_STRIPPED)) {
            stripped = true;
        }
        for (auto& s : modules) {
            if (globalMap.count(s)) {
                dependencies.push_back(globalMap[s].get());
                continue;
            }
            auto dllPath = getDllPath(directory, s);
            auto& result = globalMap.emplace(s, make_unique<Dll>(move(dllPath))).first->second;
            dependencies.push_back(result.get());
            result->fillDependencies(globalMap, recurseIntoSystem);
        }
    }

    string toString() const {
        switch (path.location) {
            case DllPath::Missing: return path.path + " (MISSING)";
            case DllPath::User: return getFileName(path.path) + (isValid ? "" : " (INVALID!)") + " (" + path.path + ")";
            case DllPath::System: return getFileName(path.path) + "(SYSTEM) (" + path.path + ")";
        }
    }
};

using uint = unsigned int;
void doWalkDependencies(
    const Dll& dll,
    set<const Dll*>& visited,
    const function<void(const Dll&, bool wasVisited, uint level)>& action,
    const function<bool(const Dll&, bool wasVisited, uint level)>& recurseFilter,
    uint level
) {
    visited.insert(&dll);
    for (auto* dependency : dll.dependencies) {
        bool wasVisited = visited.count(dependency) > 0;
        action(*dependency, wasVisited, level);
        if (!wasVisited && (!recurseFilter || recurseFilter(*dependency, wasVisited, level))) {
            doWalkDependencies(*dependency, visited, action, recurseFilter, level + 1);
        }
    }
}

void walkDependencies(
    const Dll& dll,
    const function<void(const Dll&, bool wasVisited, uint level)>& action,
    bool visitRoot = false,
    const function<bool(const Dll&, bool wasVisited, uint level)>& recurseFilter = {}
) {
    if (visitRoot) {
        action(dll, false, 0);
    }
    set<const Dll*> visitedSet;
    doWalkDependencies(dll, visitedSet, action, recurseFilter, 1);
}

void dumpDependenciesTree(
    const Dll& dll,
    bool visitRoot = true,
    const function<bool(const Dll&, bool wasDumped, uint level)>& printFilter = {},
    const function<bool(const Dll&, bool wasDumped, uint level)>& recurseFilter = {}
) {
    walkDependencies(dll, [&](const Dll& dependency, bool wasVisited, uint level) {
        string indent(level * 4, ' ');
        if (!printFilter || printFilter(dependency, wasVisited, level)) {
            cout << indent << dependency.toString() << (wasVisited ? " (+ more, see above)" : "") << endl;
        }
    }, visitRoot, recurseFilter);
}

void dumpDependenciesFlat(
    const Dll& dll,
    bool visitRoot = true,
    const function<bool(const Dll&, bool wasDumped, uint level)>& printFilter = {},
    const function<bool(const Dll&, bool wasDumped, uint level)>& recurseFilter = {}
) {
    walkDependencies(dll, [&](const Dll& dependency, bool wasVisited, uint level) {
        string indent = (level > 0 && visitRoot ? "    " : "");
        if (!printFilter || printFilter(dependency, wasVisited, level)) {
            cout << indent << dependency.toString() << (wasVisited ? " (+ more, see above)" : "") << endl;
        }
    }, visitRoot, recurseFilter);
}

void dumpFlatNonSystemDependencies(const Dll& dll) {
    dumpDependenciesFlat(
        dll,
        true,
        [](const Dll& dep, bool dumped, uint level) { return !dumped && dep.path.location != DllPath::System; }
    );
}

void dumpTreeNonSystemDependencies(const Dll& dll) {
    dumpDependenciesFlat(
        dll,
        true,
        [](const Dll& dep, bool dumped, uint level) { return !dumped && dep.path.location != DllPath::System; }
    );
}

string formatFileSize(size_t size) {
    stringstream ss;
    if (size < 1024) { ss << size << "B"; }
    else if (size < 1024*1024) { ss << setprecision(2) << fixed << size/1024.0 << "kB"; }
    else ss << setprecision(2) << fixed << size/(1024.0*1024.0) << "MB";
    return ss.str();
}

void printSizeInfo(const Dll& dll) {
    size_t total = 0;
    bool anyUnstripped = false;
    walkDependencies(dll, [&](const Dll& dependency, bool wasVisited, uint level) {
        if (dependency.path.location == DllPath::System || wasVisited) { return; }

        optional<size_t> fileSize;
        boost::system::error_code fileError;
        if (dependency.path.location != DllPath::Missing) {
            auto size = fs::file_size(dependency.path.path, fileError);
            if (!fileError) {
                fileSize = size;
                total += size;
            }
        }

        string indent = (level > 0 ? "    " : "");
        string size = fileSize ? formatFileSize(*fileSize) : "ERROR";
        if (!dependency.stripped) { anyUnstripped = true; }
        cout << indent << getFileName(dependency.path.path) << " (" << size << ")" <<
                (dependency.stripped ? "" : "*") << endl;
    }, true);
    cout << endl;
    cout << "Total: " << formatFileSize(total) << endl;
    if (anyUnstripped) {
        cout << "Files marked with * can be further stripped." << endl;
    }
}

void copyTo(const Dll& dll, const string& target, bool overwrite = false, bool includeRoot = false) {
    try {
        if (target != "." && target != "..") {
            fs::create_directories(target);
        }
    }catch(exception& e) {
        cerr << e.what() << endl;
        return;
    }
    walkDependencies(dll, [&](const Dll& dependency, bool wasVisited, uint level) {
        if (dependency.path.location != DllPath::User || wasVisited) { return; }
        try {
            if (fs::path(dependency.path.path).parent_path() == fs::path(target)) {
                return;
            }
            fs::copy_file(
                dependency.path.path,
                target + "/" + getFileName(dependency.path.path),
                overwrite ? fs::copy_option::overwrite_if_exists : fs::copy_option::none
            );
        }catch(exception& e) {
            cerr << e.what() << endl;
        }
    }, includeRoot);
}

int main(int argc, char** argv) {
    po::options_description description("Options");
    description.add_options()
        ("copy", po::value<string>()->value_name("dir"), "If specified, copy all dependencies to the specified directory.")
        ("force", po::bool_switch(), "When used with --copy, overwrite existing files.")
        ("all", po::bool_switch(), "When used with --copy, also include the input file.")
        ("help", "Prints this help message")
        ("input", po::value<vector<string>>()->value_name("file"), "The exe/dll file for which to show dependencies");
    po::positional_options_description pos;
    pos.add("input", 1);

    po::variables_map varMap;
    try {
        po::store(po::command_line_parser(argc, argv).options(description).positional(pos).run(), varMap);
        po::notify(varMap);
    }catch(exception& e) {
        cout << e.what();
        return 1;
    }

    if (varMap.count("help") || varMap.count("input") == 0) {
        cout << description << endl;
        return 0;
    }

    map<string, unique_ptr<Dll>> globalMap;
    Dll exe({ varMap["input"].as<vector<string>>().at(0), DllPath::User });
    exe.fillDependencies(globalMap);
    printSizeInfo(exe);

    if (varMap.count("copy")) {
        copyTo(exe, varMap["copy"].as<string>(), varMap["force"].as<bool>(), varMap["all"].as<bool>());
    }

    return 0;
}