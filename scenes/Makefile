.PHONY : all

#n.b. the '-y' sets autoexec scripts to 'on' so that driver expressions will work
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	BLENDER = /Applications/Blender.app/Contents/MacOS/Blender -y
else ifeq ($(UNAME_S),Linux)
	BLENDER = ../../blender-4.5.2-linux-x64/blender -y
else
	BLENDER = blender
endif

EXPORT_MESHES=export-meshes.py
EXPORT_SCENE=export-scene.py

ASSET=../assets

all : \
	$(ASSET)/cave-cart.pnct \
	$(ASSET)/cave-cart.scene \
	$(ASSET)/tunnel-cart.pnct \
	$(ASSET)/tunnel-cart.scene \


$(ASSET)/cave-cart.scene : cave-mine-cart.blend $(EXPORT_SCENE)
	$(BLENDER) --background --python $(EXPORT_SCENE) -- '$<':Main '$@'

$(ASSET)/cave-cart.pnct : cave-mine-cart.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Main '$@'
$(ASSET)/tunnel-cart.scene : cave-tunnel.blend $(EXPORT_SCENE)
	$(BLENDER) --background --python $(EXPORT_SCENE) -- '$<':Main '$@'

$(ASSET)/tunnel-cart.pnct : cave-tunnel.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Main '$@'