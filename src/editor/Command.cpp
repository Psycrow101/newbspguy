#include <string.h>
#include "Command.h"
#include "Gui.h"
#include <lodepng.h>
#include "icons/aaatrigger.h"
#include "icons/wallguard.h"
#include "Settings.h"


Command::Command(std::string _desc, int _mapIdx)
{
	this->desc = _desc;
	this->mapIdx = _mapIdx;
}

Bsp* Command::getBsp()
{
	if (mapIdx < 0 || mapIdx >= g_app->mapRenderers.size())
	{
		return NULL;
	}

	return g_app->mapRenderers[mapIdx]->map;
}

BspRenderer* Command::getBspRenderer()
{
	if (mapIdx < 0 || mapIdx >= g_app->mapRenderers.size())
	{
		return NULL;
	}

	return g_app->mapRenderers[mapIdx];
}


//
// Edit entity
//
EditEntityCommand::EditEntityCommand(std::string desc, int entIdx, Entity oldEntData, Entity newEntData)
	: Command(desc, g_app->getSelectedMapId())
{
	this->entIdx = entIdx;
	this->oldEntData = Entity();
	this->newEntData = Entity();
	this->oldEntData = oldEntData;
	this->newEntData = newEntData;
	this->allowedDuringLoad = true;
}

EditEntityCommand::~EditEntityCommand()
{
}

void EditEntityCommand::execute()
{
	Entity* target = getEnt();
	*target = newEntData;
	refresh();
}

void EditEntityCommand::undo()
{
	Entity* target = getEnt();
	*target = oldEntData;
	refresh();
}

Entity* EditEntityCommand::getEnt()
{
	Bsp* map = getBsp();

	if (!map || entIdx < 0 || entIdx >= map->ents.size())
	{
		return NULL;
	}

	return map->ents[entIdx];
}

void EditEntityCommand::refresh()
{
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	Entity* ent = getEnt();
	if (!ent)
		return;
	renderer->refreshEnt(entIdx);
	if (!ent->isBspModel())
	{
		renderer->refreshPointEnt(entIdx);
	}
	renderer->updateEntityState(entIdx);
	pickCount++; // force GUI update
	g_app->updateModelVerts();
}

size_t EditEntityCommand::memoryUsage()
{
	return sizeof(EditEntityCommand) + oldEntData.getMemoryUsage() + newEntData.getMemoryUsage();
}

//
// Delete entity
//
DeleteEntityCommand::DeleteEntityCommand(std::string desc, int entIdx)
	: Command(desc, g_app->getSelectedMapId())
{
	this->entIdx = entIdx;
	this->entData = new Entity();
	*this->entData = *(g_app->getSelectedMap()->ents[entIdx]);
	this->allowedDuringLoad = true;
}

DeleteEntityCommand::~DeleteEntityCommand()
{
	if (entData)
		delete entData;
}

void DeleteEntityCommand::execute()
{
	if (entIdx < 0)
		return;

	Bsp* map = getBsp();

	if (!map)
		return;

	g_app->deselectObject();

	Entity* ent = map->ents[entIdx];

	map->ents.erase(map->ents.begin() + entIdx);

	refresh();

	delete ent;
}

void DeleteEntityCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.insert(map->ents.begin() + entIdx, newEnt);

	g_app->pickInfo.SetSelectedEnt(entIdx);

	refresh();

	if (newEnt->hasKey("model") &&
		toLowerCase(newEnt->keyvalues["model"]).find(".bsp") != std::string::npos)
	{
		g_app->reloadBspModels();
	}
}

void DeleteEntityCommand::refresh()
{
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;

	renderer->preRenderEnts();
	g_app->gui->refresh();
}

size_t DeleteEntityCommand::memoryUsage()
{
	return sizeof(DeleteEntityCommand) + entData->getMemoryUsage();
}


//
// Create Entity
//
CreateEntityCommand::CreateEntityCommand(std::string desc, int mapIdx, Entity* entData) : Command(desc, mapIdx)
{
	this->entData = new Entity();
	*this->entData = *entData;
	this->allowedDuringLoad = true;
}

CreateEntityCommand::~CreateEntityCommand()
{
	if (entData)
	{
		delete entData;
	}
}

void CreateEntityCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);
	map->update_ent_lump();
	g_app->updateEnts();
	refresh();
}

void CreateEntityCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	g_app->deselectObject();

	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();
	refresh();
}

void CreateEntityCommand::refresh()
{
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;

	renderer->preRenderEnts();
	g_app->gui->refresh();
}

size_t CreateEntityCommand::memoryUsage()
{
	return sizeof(CreateEntityCommand) + entData->getMemoryUsage();
}


//
// Duplicate BSP Model command
//
DuplicateBspModelCommand::DuplicateBspModelCommand(std::string desc, int entIdx)
	: Command(desc, g_app->getSelectedMapId())
{
	int tmpentIdx = entIdx;
	int modelIdx = 0;
	Bsp* map = g_app->getSelectedMap();
	if (map && tmpentIdx >= 0)
	{
		modelIdx = map->ents[tmpentIdx]->getBspModelIdx();
		if (modelIdx < 0 && map->is_worldspawn_ent(tmpentIdx))
		{
			modelIdx = 0;
		}
	}

	this->oldModelIdx = modelIdx;
	this->newModelIdx = -1;
	this->entIdx = tmpentIdx;
	this->initialized = false;
	this->allowedDuringLoad = false;
	memset(&oldLumps, 0, sizeof(LumpState));
}

DuplicateBspModelCommand::~DuplicateBspModelCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void DuplicateBspModelCommand::execute()
{
	Bsp* map = getBsp();
	Entity* ent = map->ents[entIdx];
	BspRenderer* renderer = getBspRenderer();

	if (!initialized)
	{
		int dupLumps = CLIPNODES | EDGES | FACES | NODES | PLANES | SURFEDGES | TEXINFO | VERTICES | LIGHTING | MODELS;
		oldLumps = map->duplicate_lumps(dupLumps);
		initialized = true;
	}

	newModelIdx = map->duplicate_model(oldModelIdx);
	ent->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));

	renderer->updateLightmapInfos();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
	renderer->reloadLightmaps();
	renderer->addClipnodeModel(newModelIdx);

	g_app->pickInfo.selectedFaces.clear();
	if (g_app->pickInfo.selectedEnts.size())
		g_app->pickInfo.SetSelectedEnt(g_app->pickInfo.selectedEnts[0]);

	pickCount++;
	vertPickCount++;

	g_app->gui->refresh();
	/*
	if (g_app->pickInfo.entIdx[0] == entIdx) {
		g_modelIdx = newModelIdx;
		g_app->updateModelVerts();
	}
	*/
}

void DuplicateBspModelCommand::undo()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	Entity* ent = map->ents[entIdx];
	map->replace_lumps(oldLumps);
	ent->setOrAddKeyvalue("model", "*" + std::to_string(oldModelIdx));

	renderer->reload();
	g_app->gui->refresh();

	g_app->deselectObject();
	/*
	if (g_app->pickInfo.entIdx[0] == entIdx) {
		g_modelIdx = oldModelIdx;
		g_app->updateModelVerts();
	}
	*/
}

size_t DuplicateBspModelCommand::memoryUsage()
{
	int size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumpLen[i];
	}

	return size;
}


//
// Create BSP model
//
CreateBspModelCommand::CreateBspModelCommand(std::string desc, int mapIdx, Entity* entData, float size, bool empty) : Command(desc, mapIdx)
{
	this->entData = new Entity();
	*this->entData = *entData;
	this->mdl_size = size;
	this->initialized = false;
	this->empty = empty;
    this->defaultTextureName = "aaatrigger";
	memset(&oldLumps, 0, sizeof(LumpState));
}

CreateBspModelCommand::~CreateBspModelCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
	if (entData)
	{
		delete entData;
		entData = NULL;
	}
}

void CreateBspModelCommand::setDefaultTextureName(std::string textureName)
{
    defaultTextureName = std::move(textureName);
}

void CreateBspModelCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	//renderer->addNewRenderFace();
	int aaatriggerIdx = getDefaultTextureIdx();

	if (!initialized)
	{
		int dupLumps = CLIPNODES | EDGES | FACES | NODES | PLANES | SURFEDGES | TEXINFO | VERTICES | LIGHTING | MODELS;
		if (aaatriggerIdx == -1)
		{
			dupLumps |= TEXTURES;
		}
		oldLumps = map->duplicate_lumps(dupLumps);
	}

	bool NeedreloadTextures = false;
	// add the aaatrigger texture if it doesn't already exist
	if (aaatriggerIdx == -1)
	{
		NeedreloadTextures = true;
		aaatriggerIdx = addDefaultTexture();
	}

	vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
	vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
	int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, empty);
	BSPMODEL& model = map->models[modelIdx];
	if (!initialized)
	{
		entData->addKeyvalue("model", "*" + std::to_string(modelIdx));
	}

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);

	g_app->deselectObject();

	//renderer->updateLightmapInfos();
	//renderer->calcFaceMaths();
	//renderer->preRenderFaces();
	//renderer->preRenderEnts();
	//renderer->reloadTextures();
	//renderer->reloadLightmaps();
	//renderer->addClipnodeModel(modelIdx);
	//renderer->refreshModel(modelIdx);
	//

	renderer->updateLightmapInfos();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
	if (NeedreloadTextures)
		renderer->reloadTextures();
	renderer->reloadLightmaps();
	renderer->addClipnodeModel(modelIdx);
	//renderer->reload();


	g_app->gui->refresh();

	initialized = true;
}

void CreateBspModelCommand::undo()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	if (!map || !renderer)
		return;

	map->replace_lumps(oldLumps);

	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();

	renderer->reload();
	g_app->gui->refresh();
	g_app->deselectObject();
}

size_t CreateBspModelCommand::memoryUsage()
{
	int size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumpLen[i];
	}

	return size;
}

int CreateBspModelCommand::getDefaultTextureIdx()
{
	Bsp* map = getBsp();
	if (!map)
		return -1;

	unsigned int totalTextures = ((unsigned int*)map->textures)[0];
	for (unsigned int i = 0; i < totalTextures; i++)
	{
		int texOffset = ((int*)map->textures)[i + 1];
		if (texOffset >= 0)
		{
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			if (tex.szName[0] != '\0' && strcasecmp(tex.szName, defaultTextureName.c_str()) == 0)
			{
				logf("Found default texture in map file.\n");
				return i;
			}
		}
	}

	return -1;
}

int CreateBspModelCommand::addDefaultTexture()
{
	Bsp* map = getBsp();
	if (!map)
		return -1;
	unsigned char* tex_dat = NULL;
	unsigned int w, h;

    if (defaultTextureName == "wallguard")
        lodepng_decode24(&tex_dat, &w, &h, wallguard_dat, sizeof(wallguard_dat));
    else
	    lodepng_decode24(&tex_dat, &w, &h, aaatrigger_dat, sizeof(aaatrigger_dat));

	int aaatriggerIdx = map->add_texture(defaultTextureName.c_str(), tex_dat, w, h);

	delete[] tex_dat;

	return aaatriggerIdx;
}


//
// Create several BSP models
//
CreateSeveralBspModelCommand::CreateSeveralBspModelCommand(std::string desc, int mapIdx) : Command(desc, mapIdx)
{
    this->entNum = 0;
    this->initialized = false;
    this->defaultTextureName = "aaatrigger";
    memset(&oldLumps, 0, sizeof(LumpState));
}

CreateSeveralBspModelCommand::~CreateSeveralBspModelCommand()
{
    for (int i = 0; i < HEADER_LUMPS; i++)
    {
        if (oldLumps.lumps[i])
            delete[] oldLumps.lumps[i];
    }

    for (int i = 0; i < entNum; i++)
        delete entData[i];

    entData.clear();
    mins.clear();
    maxs.clear();
    empties.clear();

    entNum = 0;
}

void CreateSeveralBspModelCommand::setDefaultTextureName(std::string textureName)
{
    defaultTextureName = std::move(textureName);
}

void CreateSeveralBspModelCommand::addEnt(Entity* entData, vec3 mins, vec3 maxs, bool empty) {
    Entity* entLocalData = new Entity();
    *entLocalData = *entData;

    this->entData.push_back(entLocalData);
    this->mins.push_back(mins);
    this->maxs.push_back(maxs);
    this->empties.push_back(empty);
    this->entNum++;
}

void CreateSeveralBspModelCommand::execute()
{
    Bsp* map = getBsp();
    if (!map)
        return;
    BspRenderer* renderer = getBspRenderer();
    if (!renderer)
        return;
    //renderer->addNewRenderFace();
    int aaatriggerIdx = getDefaultTextureIdx();

    if (!initialized)
    {
        int dupLumps = CLIPNODES | EDGES | FACES | NODES | PLANES | SURFEDGES | TEXINFO | VERTICES | LIGHTING | MODELS;
        if (aaatriggerIdx == -1)
        {
            dupLumps |= TEXTURES;
        }
        oldLumps = map->duplicate_lumps(dupLumps);
    }

    bool NeedreloadTextures = false;
    // add the aaatrigger texture if it doesn't already exist
    if (aaatriggerIdx == -1)
    {
        NeedreloadTextures = true;
        aaatriggerIdx = addDefaultTexture();
    }

    for (int i = 0; i < entNum; i++) {
        int modelIdx = map->create_solid(mins[i], maxs[i], aaatriggerIdx, empties[i]);
        if (!initialized)
        {
            entData[i]->addKeyvalue("model", "*" + std::to_string(modelIdx));
        }

        Entity* newEnt = new Entity();
        *newEnt = *entData[i];
        map->ents.push_back(newEnt);

        renderer->addClipnodeModel(modelIdx);
    }

    g_app->deselectObject();

    //renderer->updateLightmapInfos();
    //renderer->calcFaceMaths();
    //renderer->preRenderFaces();
    //renderer->preRenderEnts();
    //renderer->reloadTextures();
    //renderer->reloadLightmaps();
    //renderer->addClipnodeModel(modelIdx);
    //renderer->refreshModel(modelIdx);
    //

    renderer->updateLightmapInfos();
    renderer->calcFaceMaths();
    renderer->preRenderFaces();
    renderer->preRenderEnts();
    if (NeedreloadTextures)
        renderer->reloadTextures();
    renderer->reloadLightmaps();
    //renderer->reload();


    g_app->gui->refresh();

    initialized = true;
}

void CreateSeveralBspModelCommand::undo()
{
    Bsp* map = getBsp();
    BspRenderer* renderer = getBspRenderer();

    if (!map || !renderer)
        return;

    map->replace_lumps(oldLumps);

    for (int i = 0; i < entNum; i++) {
        delete map->ents[map->ents.size() - 1];
        map->ents.pop_back();
    }

    renderer->reload();
    g_app->gui->refresh();
    g_app->deselectObject();
}

size_t CreateSeveralBspModelCommand::memoryUsage()
{
    int size = sizeof(DuplicateBspModelCommand) * entNum;

    for (int i = 0; i < HEADER_LUMPS; i++)
    {
        size += oldLumps.lumpLen[i];
    }

    return size;
}

int CreateSeveralBspModelCommand::getDefaultTextureIdx()
{
    Bsp* map = getBsp();
    if (!map)
        return -1;

    unsigned int totalTextures = ((unsigned int*)map->textures)[0];
    for (unsigned int i = 0; i < totalTextures; i++)
    {
        int texOffset = ((int*)map->textures)[i + 1];
        if (texOffset >= 0)
        {
            BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
            if (tex.szName[0] != '\0' && strcasecmp(tex.szName, defaultTextureName.c_str()) == 0)
            {
                logf("Found default texture in map file.\n");
                return i;
            }
        }
    }

    return -1;
}

int CreateSeveralBspModelCommand::addDefaultTexture()
{
    Bsp* map = getBsp();
    if (!map)
        return -1;
    unsigned char* tex_dat = NULL;
    unsigned int w, h;

    if (defaultTextureName == "wallguard")
        lodepng_decode24(&tex_dat, &w, &h, wallguard_dat, sizeof(wallguard_dat));
    else
        lodepng_decode24(&tex_dat, &w, &h, aaatrigger_dat, sizeof(aaatrigger_dat));

    int aaatriggerIdx = map->add_texture(defaultTextureName.c_str(), tex_dat, w, h);

    delete[] tex_dat;

    return aaatriggerIdx;
}


//
// Edit BSP model
//
EditBspModelCommand::EditBspModelCommand(std::string desc, int entIdx, LumpState oldLumps, LumpState newLumps,
	vec3 oldOrigin) : Command(desc, g_app->getSelectedMapId())
{

	this->oldLumps = oldLumps;
	this->newLumps = newLumps;
	this->allowedDuringLoad = false;
	this->oldOrigin = oldOrigin;

	this->entIdx = entIdx;

	Bsp* map = g_app->getSelectedMap();
	if (map && entIdx >= 0)
	{
		this->modelIdx = map->ents[entIdx]->getBspModelIdx();
		this->newOrigin = map->ents[entIdx]->getOrigin();
	}
	else
	{
		this->modelIdx = -1;
		this->newOrigin = this->oldOrigin;
	}
}

EditBspModelCommand::~EditBspModelCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
		if (newLumps.lumps[i])
			delete[] newLumps.lumps[i];
	}
}

void EditBspModelCommand::execute()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	if (!map || !renderer)
		return;

	map->replace_lumps(newLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());
	map->getBspRender()->undoEntityState[entIdx].setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());

	refresh();
}

void EditBspModelCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	map->getBspRender()->undoEntityState[entIdx].setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	refresh();
}

void EditBspModelCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();

	renderer->updateLightmapInfos();
	renderer->calcFaceMaths();
	renderer->refreshModel(modelIdx);
	renderer->refreshEnt(entIdx);
	g_app->gui->refresh();
	renderer->saveLumpState(0xffffff, true);
	renderer->updateEntityState(entIdx);

	if (g_app->pickInfo.GetSelectedEnt() == entIdx)
	{
		g_app->updateModelVerts();
	}
}

size_t EditBspModelCommand::memoryUsage()
{
	int size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumpLen[i] + newLumps.lumpLen[i];
	}

	return size;
}



//
// Clean Map
//
CleanMapCommand::CleanMapCommand(std::string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx)
{
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

CleanMapCommand::~CleanMapCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void CleanMapCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	logf("Cleaning {}\n", map->bsp_name);
	map->remove_unused_model_structures().print_delete_stats(1);

	refresh();
}

void CleanMapCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);

	refresh();
}

void CleanMapCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	renderer->saveLumpState(0xffffffff, true);
}

size_t CleanMapCommand::memoryUsage()
{
	int size = sizeof(CleanMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumpLen[i];
	}

	return size;
}



//
// Optimize Map
//
OptimizeMapCommand::OptimizeMapCommand(std::string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx)
{
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

OptimizeMapCommand::~OptimizeMapCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i])
			delete[] oldLumps.lumps[i];
	}
}

void OptimizeMapCommand::execute()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	if (!map || !renderer)
		return;
	map->update_ent_lump();

	logf("Optimizing {}\n", map->bsp_name);
	if (!map->has_hull2_ents())
	{
		logf("    Redirecting hull 2 to hull 1 because there are no large monsters/pushables\n");
		map->delete_hull(2, 1);
	}

	bool oldVerbose = g_verbose;
	g_verbose = true;
	map->delete_unused_hulls(true).print_delete_stats(1);
	g_verbose = oldVerbose;

	refresh();
}

void OptimizeMapCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);

	refresh();
}

void OptimizeMapCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
	renderer->saveLumpState(0xffffffff, true);
}

size_t OptimizeMapCommand::memoryUsage()
{
	int size = sizeof(OptimizeMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumpLen[i];
	}

	return size;
}