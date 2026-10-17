# Vivado 2026.1 startup guard for this migrated project (v2).
# Keep GUI, batch and OOC workers from refreshing Tcl Store through Git.
set_param tclapp.enableGitAccess 0

# Do NOT rewrite the shared persistent manifest from startup callbacks: multiple
# OOC processes and an older GUI can read/write it concurrently. Legacy entries
# must be removed once, with all Vivado processes closed, by:
#   build/scripts/repair_vivado_startup.ps1
# Some startup work precedes this init file; changing a manifest here is too late
# to remove errors already recorded in a worker's message database.
