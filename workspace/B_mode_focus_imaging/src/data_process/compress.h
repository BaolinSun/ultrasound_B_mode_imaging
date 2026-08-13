#ifndef __DATA_PROCESS_H
#define __DATA_PROCESS_H

#include "../main.h"

/************²î·Ö+»ô·òÂüÑ¹Ëõ****************/
//
typedef struct node {
	int value;
	int freq;
	char code[20];
	struct node *left;
	struct node *right;
	struct node *parent;
} node;

typedef struct StackNode {
	struct node *node;
	struct StackNode *next;
	u16 id;
// char code[20];
} StackNode;

typedef struct {
	u16 *freq_table;
	u16 freq_table_length;
	u8 *encode_data;
	u8 padding_length;
} hulfman_encode_data;

int compare(const void *a, const void *b);
node *create_node(int value, int freq);
node *merge_nodes(node *left, node *right);
StackNode* newStackNode(node *node);
int isStackEmpty(StackNode *root);
void push(StackNode** root, node *node, const char *code);
StackNode* pop(StackNode** root);
node *build_huffman_tree(node *nodes[], int count);
void generate_huffman_codes(node* tree_root, char *codes[]);
char *encode_data(u16 *data, int length, char *codes[]);
u8 *str_to_int(char *str, int length);
u16 *diff(u16 *data, u16 length);
hulfman_encode_data *hulfman_encode(u16 *data, u16 data_length);
/*******************************************************/

/**********Î»ÊýÑ¹Ëõ**********/
u32 data_bits_compress(u16 *data, u32 data_length);
u32 fast_data_bits_compress(u16 *data, u32 data_length);

#endif
