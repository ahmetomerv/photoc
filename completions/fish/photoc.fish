# Native fish completion; no photoc invocation or external helper dependency.

function __fish_photoc_command
    set -l tokens (commandline -opc)
    set -e tokens[1]
    set -l pending 0
    set -l ended 0
    for token in $tokens
        if test $pending -eq 1
            set pending 0
            continue
        end
        if test $ended -eq 0
            switch $token
                case --
                    set ended 1
                    continue
                case --quality --target --min-quality --output-dir --output --columns --thumb-size --sort --show --images --state --by --gap --format --threshold --camera --make --iso --aperture --focal --after --before
                    set pending 1
                    continue
                case '-*'
                    continue
            end
        end
        printf '%s\n' $token
        return 0
    end
    return 1
end

function __fish_photoc_using_command
    contains -- (__fish_photoc_command) $argv
end

function __fish_photoc_options
    set -l tokens (commandline -opc)
    not contains -- -- $tokens
end

function __fish_photoc_path
    set -l tokens (commandline -opc)
    # Option arguments have their own rules; do not offer input paths there.
    not contains -- $tokens[-1] --quality --target --min-quality --output-dir --output --columns --thumb-size --sort --show --images --state --by --gap --format --threshold --camera --make --iso --aperture --focal --after --before
end

complete -c photoc -f
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a query -d 'Search JPEG metadata'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a check -d 'Audit JPEG structural readability'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a compress -d 'Re-encode JPEGs by quality or target size'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a contact -d 'Generate paged JPEG contact sheets'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a exif -d 'Inspect JPEG and Sony ARW metadata'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a duplicates -d 'Find exact duplicate files'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a stats -d 'Summarize JPEG and Sony ARW collections'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a timeline -d 'Summarize shooting dates and sessions'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a rename -d 'Preview or apply photo renames'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a sort -d 'Preview or apply photo sorting'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a focus -d 'Compare JPEG sharpness scores'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a scrub -d 'Remove EXIF GPS tags'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -a review -d 'Interactively mark JPEGs for review'

complete -c photoc -n __fish_photoc_options -s h -l help -d 'Show help'
complete -c photoc -n __fish_photoc_options -s v -l verbose -d 'Add diagnostics on stderr'
complete -c photoc -n __fish_photoc_options -s q -l quiet -d 'Suppress status and non-critical warnings'
complete -c photoc -n __fish_photoc_options -l no-progress -d 'Disable interactive progress display'
complete -c photoc -n '__fish_photoc_options; and not __fish_photoc_command >/dev/null' -s V -l version -d 'Show version'
complete -c photoc -n '__fish_photoc_options; and begin; not __fish_photoc_command >/dev/null; or __fish_photoc_using_command query check exif stats timeline duplicates focus; end' -l json -d 'Print structured JSON where supported'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query check compress contact duplicates stats timeline rename sort focus scrub review' -l recursive -d 'Include nested directories'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command check' -l only-errors -d 'List ERROR rows only'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command rename sort' -l apply -d 'Apply the plan after preflight'

complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l camera -x -d 'Exact camera model'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l make -x -d 'Exact camera make'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l iso -x -d 'Numeric ISO comparison'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l aperture -x -d 'F-number comparison'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l focal -x -d 'Focal length comparison in mm'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l after -x -d 'Inclusive start date YYYY-MM-DD'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l before -x -d 'Inclusive end date YYYY-MM-DD'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l has-gps -d 'Require valid GPS coordinates'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l no-gps -d 'Require no valid GPS coordinates'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command query' -l print0 -d 'Print NUL-separated paths'

complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command compress' -l quality -x -a '60 70 75 80 85 90 95 100' -d 'JPEG quality (default 80)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command compress' -l target -x -a '500KB 1MB 2MB 5MB' -d 'Desired maximum size'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command compress' -l min-quality -x -a '20 30 40 50' -d 'Minimum target quality (default 20)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command compress' -l output-dir -r -f -a '(__fish_complete_directories)' -d 'Destination directory'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l output -r -F -d 'Required JPEG output path'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l columns -x -a '2 3 4 5 6' -d 'Columns per sheet (default 4)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l thumb-size -x -a '96 160 240 320 512' -d 'Thumbnail size (default 240)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l quality -x -a '60 70 75 80 85 90 95 100' -d 'JPEG quality (default 85)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l metadata -d 'Show exposure metadata'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command contact' -l sort -x -a 'name date' -d 'Sort by filename or capture time'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command review' -l sort -x -a 'name date' -d 'Sort by filename or valid capture time'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command review' -l show -x -a 'all unmarked picked rejected' -d 'Filter navigation by mark'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command review' -l images -x -a 'auto iterm none' -d 'Inline image mode'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command review' -l state -r -F -d 'Review state JSON path'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command rename' -l format -x -d 'Filename template'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command sort' -l by -x -a 'date session' -d 'Grouping mode'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command sort timeline' -l gap -x -a '30m 60m 2h' -d 'Session gap (default 60m)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command focus' -l threshold -x -d 'Review cutoff (default 100)'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command focus' -l only-blurry -d 'List only scores below the threshold'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command scrub' -l gps -d 'Remove EXIF GPS tags'
complete -c photoc -n '__fish_photoc_options; and __fish_photoc_using_command scrub' -l in-place -d 'Replace originals without a backup'

complete -c photoc -n '__fish_photoc_path; and __fish_photoc_using_command duplicates stats timeline rename sort contact review' -a '(__fish_complete_directories)'
complete -c photoc -n '__fish_photoc_path; and __fish_photoc_using_command query check compress exif focus scrub' -F
