#pragma once
/* 最小替身（syntax_check.py） */
typedef enum { QOS_CLASS_USER_INTERACTIVE = 0x21, QOS_CLASS_USER_INITIATED = 0x19, QOS_CLASS_DEFAULT = 0x15, QOS_CLASS_UTILITY = 0x11, QOS_CLASS_BACKGROUND = 0x09, QOS_CLASS_UNSPECIFIED = 0 } qos_class_t;
int pthread_set_qos_class_self_np(qos_class_t, int); qos_class_t qos_class_self(void);
