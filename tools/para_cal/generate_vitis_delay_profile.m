function output_path = generate_vitis_delay_profile(output_path)
%GENERATE_VITIS_DELAY_PROFILE Generate the PS boot-time delay_profile.c.
%
% This intentionally does not update any PC/GUI JSON artifact.

this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(fileparts(this_dir));
if nargin < 1 || isempty(output_path)
    output_path = fullfile(project_root, 'build', 'src', 'ps', ...
        'bram', 'delay_profile.c');
end

delay_words = para_transmit_delay([], false);
assert(numel(delay_words) == 16384, ...
    'The Vitis delay profile must contain exactly 16384 words.');

file_id = fopen(output_path, 'w');
assert(file_id >= 0, 'Unable to open %s for writing.', output_path);
cleanup = onCleanup(@() fclose(file_id)); %#ok<NASGU>

fprintf(file_id, '#include "bram.h"\n\n');
fprintf(file_id, 'u32 delay_profile[16384] = {\n');
for first_word = 1:8:numel(delay_words)
    fprintf(file_id, '\t');
    for word_index = first_word:first_word+7
        fprintf(file_id, '0x%08X,', delay_words(word_index));
        if word_index ~= first_word + 7
            fprintf(file_id, ' ');
        end
    end
    fprintf(file_id, '\n');
end
fprintf(file_id, '};\n');
end

