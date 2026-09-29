# Vivado 2026.1 startup guard for this migrated project.
# Keep batch/OOC workers from refreshing the shared per-user Tcl Store.
set_param tclapp.enableGitAccess 0

# Active-HDL is not used by this project (TARGET_SIMULATOR is XSim).
# A stale persistent entry from the old installation splits Windows drive paths at
# the colon and causes [Common 17-1257]/[Common 17-354] in OOC workers.
# Filter only that entry before Vivado calls tclapp::load_apps; keep any other apps.
set tclapp_manifest [file normalize [file join [file dirname [info script]] .. tclapp manifest.tcl]]
if {[file exists $tclapp_manifest]} {
    set tclapp_file [open $tclapp_manifest r]
    set tclapp_lines [split [read $tclapp_file] "\n"]
    close $tclapp_file

    set tclapp_kept_lines {}
    set tclapp_changed 0
    foreach tclapp_line $tclapp_lines {
        if {[string first "aldec::activehdl" $tclapp_line] >= 0} {
            set tclapp_changed 1
        } else {
            lappend tclapp_kept_lines $tclapp_line
        }
    }

    if {$tclapp_changed} {
        set tclapp_file [open $tclapp_manifest w]
        puts -nonewline $tclapp_file [join $tclapp_kept_lines "\n"]
        close $tclapp_file
    }
}

unset -nocomplain tclapp_manifest tclapp_file tclapp_lines tclapp_kept_lines tclapp_changed tclapp_line
