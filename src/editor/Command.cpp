#include "lang.h"
#include <string.h>
#include "Command.h"
#include "Gui.h"
#include <lodepng.h>
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
	BspRenderer* renderer = getBspRenderer();
	renderer->saveEntityState(entIdx);
}

void EditEntityCommand::undo()
{
	Entity* target = getEnt();
	*target = oldEntData;
	refresh();
	BspRenderer* renderer = getBspRenderer();
	renderer->saveEntityState(entIdx);
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
	this->allowedDuringLoad = false;
}

DuplicateBspModelCommand::~DuplicateBspModelCommand()
{
}

void DuplicateBspModelCommand::execute()
{
	Bsp* map = getBsp();
	Entity* ent = map->ents[entIdx];
	BspRenderer* renderer = getBspRenderer();

	//int dupLumps = FL_CLIPNODES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS;
	oldLumps = map->duplicate_lumps(0xFFFFFFFF);

	newModelIdx = map->duplicate_model(oldModelIdx);

	ent->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));

	map->remove_unused_model_structures(CLEAN_LEAVES);

	renderer->loadLightmaps();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
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


	g_app->deselectObject();

	map->replace_lumps(oldLumps);

	map->update_ent_lump();


	Entity* ent = map->ents[entIdx];
	ent->setOrAddKeyvalue("model", "*" + std::to_string(oldModelIdx));


	renderer->loadLightmaps();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
	renderer->reloadTextures();
	g_app->gui->refresh();

	/*
	if (g_app->pickInfo.entIdx[0] == entIdx) {
		g_modelIdx = oldModelIdx;
		g_app->updateModelVerts();
	}
	*/
}

size_t DuplicateBspModelCommand::memoryUsage()
{
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
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
	this->empty = empty;
	oldLumps.lumps->clear();
}

CreateBspModelCommand::~CreateBspModelCommand()
{
	if (entData)
	{
		delete entData;
		entData = NULL;
	}
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

	int dupLumps = FL_CLIPNODES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS;
	if (aaatriggerIdx == -1)
	{
		dupLumps |= FL_TEXTURES;
	}
	oldLumps = map->duplicate_lumps(dupLumps);

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
	//BSPMODEL& model = map->models[modelIdx];

	entData->addKeyvalue("model", "*" + std::to_string(modelIdx));

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

	renderer->loadLightmaps();
	renderer->calcFaceMaths();
	renderer->preRenderFaces();
	renderer->preRenderEnts();
	if (NeedreloadTextures)
		renderer->reloadTextures();
	renderer->addClipnodeModel(modelIdx);
	//renderer->reload();


	g_app->gui->refresh();
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
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
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
			if (tex.szName[0] != '\0' && strcasecmp(tex.szName, "aaatrigger") == 0)
			{
				print_log(get_localized_string(LANG_0295));
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
	int aaatriggerIdx = map->add_texture("aaatrigger", missingTex->data, missingTex->width, missingTex->height);

	return aaatriggerIdx;
}

//
// Edit BSP model
//
EditBspModelCommand::EditBspModelCommand(std::string desc, int entIdx, LumpState oldLumps, LumpState newLumps,
	vec3 oldOrigin, unsigned int targetLumps) : Command(desc, g_app->getSelectedMapId())
{

	this->oldLumps = oldLumps;
	this->newLumps = newLumps;
	this->targetLumps = targetLumps;
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
		oldLumps.lumps[i].clear();
		newLumps.lumps[i].clear();
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
	map->getBspRender()->undoEntityStateMap[entIdx].setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());



	refresh();
}

void EditBspModelCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	map->getBspRender()->undoEntityStateMap[entIdx].setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	refresh();

	BspRenderer* renderer = getBspRenderer();

	if (targetLumps & FL_VERTICES)
	{
		if (renderer)
		{
			renderer->calcFaceMaths();
			renderer->preRenderFaces();
		}
	}

	if (targetLumps & FL_TEXTURES)
	{
		if (renderer)
		{
			renderer->reloadTextures();
		}
	}
}

void EditBspModelCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();

	renderer->calcFaceMaths();
	renderer->refreshModel(modelIdx);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (i == LUMP_LIGHTING && newLumps.lumps[i].size())
		{
		//	renderer->updateLightmapInfos();
			map->getBspRender()->loadLightmaps();
		}
		else if (i == LUMP_ENTITIES && newLumps.lumps[i].size())
		{
			renderer->refreshEnt(entIdx);
		}
	}

	g_app->gui->refresh();

	if (g_app->pickInfo.GetSelectedEnt() == entIdx)
	{
		g_app->updateModelVerts();
	}
}

size_t EditBspModelCommand::memoryUsage()
{
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size() + newLumps.lumps[i].size();
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
		oldLumps.lumps[i].clear();
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
	print_log(get_localized_string(LANG_0296),map->bsp_name);
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
}

size_t CleanMapCommand::memoryUsage()
{
	size_t size = sizeof(CleanMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
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
		oldLumps.lumps[i].clear();
	}
}

void OptimizeMapCommand::execute()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	if (!map || !renderer)
		return;
	map->update_ent_lump();

	print_log(get_localized_string(LANG_0297),map->bsp_name);
	if (!map->has_hull2_ents())
	{
		print_log(get_localized_string(LANG_0298));
		map->delete_hull(2, 1);
	}

	bool oldVerbose = g_verbose;
	g_verbose = true;
	auto removestats = map->delete_unused_hulls(true);

	removestats.print_delete_stats(1);
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
}

size_t OptimizeMapCommand::memoryUsage()
{
	size_t size = sizeof(OptimizeMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
	}

	return size;
}