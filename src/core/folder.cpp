/*
 * Copyright (C) 2013-2014  LINK/2012 <dma_2012@hotmail.com>
 * Licensed under GNU GPL v3, see LICENSE at top level directory.
 * 
 */
#include "loader.hpp"
#include <shlwapi.h>
#include <modloader/util/ini.hpp>
using namespace modloader;

// TODO priority set at runtime

template<class L>
static void BuildGlobString(const L& list, std::string& glob)
{
    glob.clear();
    for(auto& g : list) glob.append(g).push_back(';');
}

static bool MatchGlob(const std::string& name, const std::string& glob)
{
    return !glob.empty() && PathMatchSpecA(name.c_str(), glob.c_str()) != 0;
}



/*
 *  FolderInformation::Clear
 *      Clears this object
 */
void Loader::FolderInformation::Clear()
{
    mods.clear();
    childs.clear();
    mods_priority.clear();
    include_mods.clear();
    exclude_files.clear();
    RebuildExcludeFilesGlob();
    RebuildIncludeModsGlob();
}


/*
 *  FolderInformation::IsIgnored
 *      Checks if the specified mod @name (normalized) should be ignored
 *      This doesn't check parents IsIgnored
 */
bool Loader::FolderInformation::IsIgnored(const std::string& name)
{
    // If excluse all is under effect, check if we are included, otherwise check if we are excluded.
    if(this->bExcludeAll || this->bForceExclude)
        return (MatchGlob(name, include_mods_glob) == false);
    else
    {
        auto it = mods_priority.find(name);
        return (it != mods_priority.end() && it->second == 0);
    }
}

/*
 *  FolderInformation::IsFileIgnored
 *      Checks if the specified file @name (normalized) should be ignored
 *      This also checks on parents
 */
bool Loader::FolderInformation::IsFileIgnored(const std::string& name)
{
    if(MatchGlob(name, exclude_files_glob)) return true;
    return (this->parent? parent->IsFileIgnored(name) : false);
}


/*
 *  FolderInformation::AddChild
 *      Adds or finds a child FolderInformation at @path (normalized) into this
 */
auto Loader::FolderInformation::AddChild(const std::string& path) -> FolderInformation&
{
    auto ipair = childs.emplace(std::piecewise_construct,
                        std::forward_as_tuple(path),
                        std::forward_as_tuple(path, this));
    
    return ipair.first->second;
}

/*
 *  FolderInformation::AddMod
 *      Adds or finds mod at folder @name (non-normalized) into this FolderInformation
 */
auto Loader::FolderInformation::AddMod(const std::string& name) -> ModInformation&
{
    auto ipair = mods.emplace(std::piecewise_construct,
                        std::forward_as_tuple(NormalizePath(name)),
                        std::forward_as_tuple(name, *this, loader.PickUniqueModId()));
    
    return ipair.first->second;
}

/*
 *  FolderInformation::SetPriority
 *      Sets the @priority for the next mods named @name (normalized) over here
 */
void Loader::FolderInformation::SetPriority(std::string name, int priority)
{
    mods_priority.emplace(std::move(name), priority);
}


/*
 *  FolderInformation::GetPriority
 *      Gets the @priority for the mod named @name (normalized)
 */
int Loader::FolderInformation::GetPriority(const std::string& name)
{
    auto it = mods_priority.find(name);
    return (it == mods_priority.end()? default_priority : it->second);
}

/*
 *  FolderInformation::Include
 *      Adds a file @name (normalized) to the inclusion list, to be included even if ExcludeAllMods=true
 */
void Loader::FolderInformation::Include(std::string name)
{
    include_mods.emplace(std::move(name));
    RebuildIncludeModsGlob();
}

/*
 *  FolderInformation::IgnoreFileGlob
 *      Adds a file @glob (normalized) to be ignored
 */
void Loader::FolderInformation::IgnoreFileGlob(std::string glob)
{
    exclude_files.emplace(std::move(glob));
    RebuildExcludeFilesGlob();
}


/*
 *  FolderInformation::RebuildExcludeFilesGlob  - Rebuilds glob for files exclusion
 *  FolderInformation::RebuildIncludeModsGlob   - Rebuilds glob for files inclusion
 */

void Loader::FolderInformation::RebuildExcludeFilesGlob()
{
    BuildGlobString(this->exclude_files, this->exclude_files_glob);
}

void Loader::FolderInformation::RebuildIncludeModsGlob()
{
    BuildGlobString(this->include_mods, this->include_mods_glob);
}


/*
 *  FolderInformation::SetIgnoreAll     - Ignores all mods 
 *  FolderInformation::SetExcludeAll    - Excludes all mods except the ones being included ([IncludeMods])
 *  FolderInformation::SetForceExclude  - Internal ExcludeAll for -mod command line
 * 
 */

void Loader::FolderInformation::SetIgnoreAll(bool bSet)
{
    this->bIgnoreAll = bSet;
}

void Loader::FolderInformation::SetExcludeAll(bool bSet)
{
    this->bExcludeAll = bSet;
}

void Loader::FolderInformation::SetForceExclude(bool bSet)
{
    this->bForceExclude = bSet;
}



/*
 *  FolderInformation::GetAll 
 *      Gets all the childs and subchilds FolderInformation including self
 */
auto Loader::FolderInformation::GetAll() -> ref_list<FolderInformation>
{
    ref_list<FolderInformation> list = { *this };  // self
    for(auto& pair : this->childs)
    {
        auto rest = pair.second.GetAll();
        list.insert(list.end(), rest.begin(), rest.end());  // subchilds
    }
    return list;
}

/*
 *  FolderInformation::GetModsByPriority 
 *      Gets my mods ordered by priority
 */
auto Loader::FolderInformation::GetModsByPriority() -> ref_list<ModInformation>
{
    auto list = refs_mapped(this->mods);
    std::sort(list.begin(), list.end(), PriorityPred<ModInformation>());
    return list;
}


/*
 *  FolderInformation::Scan
 *      Scans mods at this and child folders
 *      This method only scans, to update using the scanned information, call Update()
 */
void Loader::FolderInformation::Scan()
{
    scoped_gdir xdir(this->path.c_str());
    Log("\n\nScanning mods at '%s'...", this->path.c_str());

    bool fine = true;
    
    // Loads the config file only once
    if(!this->gotConfig)
    {
        this->gotConfig = true;
        this->LoadConfigFromINI("modloader.ini");
    }

    // > Status here is Status::Unchanged
    // Mark all current mods as removed
    MarkStatus(this->mods, Status::Removed);

    // Walk on this folder to find mods
    if (this->bIgnoreAll == false)
    {
        fine = FilesWalk("", "*.*", false, [this](FileWalkInfo & file)
        {
            if(file.is_dir)
            {
                if (IsIgnored(NormalizePath(file.filename)))
                    Log("Ignoring mod at '%s'", file.filepath);
                else
                    this->AddMod(file.filename).Scan();
            }
            return true;
        });
    }
    
    // Find the underlying status of this folder
    UpdateStatus(*this, this->mods, fine);
    
    // Scan on my childs too
    if(this->status != Status::Removed)
    {
        for(auto& pair : this->childs)
        {
            FolderInformation& child = pair.second;
            
            child.Scan();
            if(this->status == Status::Unchanged && child.status != Status::Unchanged)
                this->status = Status::Updated;
        }
    }
}

/*
 *  FolderInformation::Update
 *      Updates the state of all mods.
 *      This is normally called after Scan()
 */
void Loader::FolderInformation::Update()
{
    if(this->status != Status::Unchanged)
    {
        Log("\nUpdating mods for '%s'...", this->path.c_str());

        auto mods = this->GetModsByPriority();

        // Uninstall all removed files since the last update...
        for(auto& mod : mods)
        {
            mod.get().ExtinguishNecessaryFiles();
        }

        // Install all updated and added files since the last update...
        for(auto& mod : mods)
        {
            mod.get().InstallNecessaryFiles();
            mod.get().SetUnchanged();
        }

        // Update my childs
        for(auto& child : this->childs)
        {
            child.second.Update();
        }

        // Collect garbaged mods and childs
        CollectInformation(this->mods);
        CollectInformation(this->childs);
        this->SetUnchanged();
    }
}

/*
 *  FolderInformation::Update
 *      Updates the state of the specified @mod
 */
void Loader::FolderInformation::Update(ModInformation& mod)
{
    mod.parent.Update();
}


/*
 *  FolderInformation::LoadConfigFromINI
 *      Loads configuration specific to this folder from the specified ini file
 */
void Loader::FolderInformation::LoadConfigFromINI(const std::string& inifile)
{
    modloader::ini cfg;

    // Reads the top [Config] section
    auto ReadConfig = [this](const modloader::ini::key_container& kv)
    {
        for(auto& pair : kv)
        {
            if(!compare(pair.first, "IgnoreAllFiles", false))
                this->SetIgnoreAll(to_bool(pair.second));
            else if(!compare(pair.first, "ExcludeAllMods", false))
                this->SetExcludeAll(to_bool(pair.second));
        }
    };

    // Reads the [Priority] section
    auto ReadPriorities = [this](const modloader::ini::key_container& kv)
    {
        for(auto& pair : kv) this->SetPriority(NormalizePath(pair.first), std::strtol(pair.second.c_str(), 0, 0));
    };

    // Reads the [ExcludeFiles] section
    auto ReadExcludeFiles = [this](const modloader::ini::key_container& kv)
    {
        for(auto& pair : kv) this->IgnoreFileGlob(NormalizePath(pair.first));
    };

    // Reads the [IncludeFiles] section
    auto ReadIncludeMods = [this](const modloader::ini::key_container& kv)
    {
        for(auto& pair : kv) this->Include(NormalizePath(pair.first));
    };

    // Load the ini and readddddddddddddd
    if(cfg.load_file(inifile))
    {
        ReadConfig(cfg["Config"]);
        ReadPriorities(cfg["Priority"]);
        ReadExcludeFiles(cfg["ExcludeFiles"]);
        ReadIncludeMods(cfg["IncludeMods"]);
    }
    else
        Log("Failed to load config file");

}