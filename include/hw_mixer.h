// hw_mixer.h
#ifndef HW_MIXER_H
#define HW_MIXER_H

int init_hw_mixer(unsigned int card, const char *mixer_path_xml);
int set_hw_mixer_path(const char *path_name);
void cleanup_hw_mixer(void);

#endif // HW_MIXER_H
