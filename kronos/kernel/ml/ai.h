#ifndef AI_H
#define AI_H

#include "core.h"

void ai_init(void);
void ai_draw(int id);
void ai_keypress(int id, char c);
void ai_get_info(char *buf, int max);
void ai_create_net(int nl, int *sz);
int ai_train_net(int epochs, int lr_int);
int ai_save_model(const char *path);
int ai_load_model(const char *path);
int ai_export_txt(const char *path);
int ai_infer_str(const char *in_str, char *out_str, int max);
void ai_set_act(int layer, int act);
void ai_set_auto(int on);
void ai_set_lr(int lr_int);
void ai_set_epochs(int ep);
void ai_set_mu(int mu_int);
void ai_set_lr_decay(int dec_int);
void ai_set_val_pct(int pct);
void ai_set_batch(int bs);
void ai_set_opt(int opt);
void ai_set_adam_beta1(int b1);
void ai_set_adam_beta2(int b2);
void ai_set_weight_decay(int wd);
void ai_set_clip(int clip);
void ai_set_es_patience(int ep);
void ai_set_lr_step(int step);
void ai_set_lr_factor(int fact);
int ai_load_dataset(const char *path);
void ai_bg_task(void);
void ai_open_trainer(int si);
void ai_open_editor(int si);
void ai_open_ds_view(void);
void ai_open_weights(int si);
int ai_select_slot(int si);
int ai_get_slot_count(void);
int ai_slot_ready(int si);
int ai_infer_slot(int si, int *in, int *out);
int ai_save_model_slot(int si, const char *path);
int ai_load_model_slot(int si, const char *path);
int ai_export_text_ds(const char *path);
int ai_import_text_ds(const char *path, int ni, int no);
int ds_create_mgr(int ni, int no);
int ds_add_sample(const char *str);
int ds_save_mgr(const char *path);
int ds_mgr_count(void);
void ds_clear_mgr(void);
int ds_generate(const char *type);

#endif
