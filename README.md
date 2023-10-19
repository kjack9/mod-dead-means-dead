# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# Dead Means Dead<br><sub>Creature Respawn Customization Module</sub>

## What is this?
This module allows an AzerothCore server administrator to disable or customize creature respawn times in dungeons, raids, or in the world.

## Defaults
By default, the module will disable respawns in dungeons and raids - "dead" means "**DEAD**". :skull:

## Configuration
If you'd rather respawn times be longer, shorter, or applied in different map/area types, please edit the configuration file. To do this, find the `mod_dead_means_dead.conf.dist` file inside your `etc/modules/` directory. Make a copy of it called `mod_dead_means_dead.conf`. Make all configuration changes in the `.conf` file only.

The configuration options are all well-described inside the configuration file.

You may reload the configuration by issuing the `.reload config` command from a GM character or the `reload config` command from the worldserver console. Note that existing dead creatures will not be updated if you change your settings, but any newly-dead creatures will use the updated values.
