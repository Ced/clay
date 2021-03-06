
   /*--------------------------------------------------------------------+
    |                              Clay                                  |
    |--------------------------------------------------------------------|
    |                           transformation.c                         |
    |--------------------------------------------------------------------|
    |                    First version: 03/04/2012                       |
    +--------------------------------------------------------------------+

 +--------------------------------------------------------------------------+
 |  / __)(  )    /__\ ( \/ )                                                |
 | ( (__  )(__  /(__)\ \  /         Chunky Loop Alteration wizardrY         |
 |  \___)(____)(__)(__)(__)                                                 |
 +--------------------------------------------------------------------------+
 | Copyright (C) 2012 University of Paris-Sud                               |
 |                                                                          |
 | This library is free software; you can redistribute it and/or modify it  |
 | under the terms of the GNU Lesser General Public License as published by |
 | the Free Software Foundation; either version 2.1 of the License, or      |
 | (at your option) any later version.                                      |
 |                                                                          |
 | This library is distributed in the hope that it will be useful but       |
 | WITHOUT ANY WARRANTY; without even the implied warranty of               |
 | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser  |
 | General Public License for more details.                                 |
 |                                                                          |
 | You should have received a copy of the GNU Lesser General Public License |
 | along with this software; if not, write to the Free Software Foundation, |
 | Inc., 51 Franklin Street, Fifth Floor,                                   |
 | Boston, MA  02110-1301  USA                                              |
 |                                                                          |
 | Clay, the Chunky Loop Alteration wizardrY                                |
 | Written by Joel Poudroux, joel.poudroux@u-psud.fr                        |
 +--------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <osl/macros.h>
#include <osl/scop.h>
#include <osl/body.h>
#include <osl/strings.h>
#include <osl/util.h>
#include <osl/statement.h>
#include <osl/relation.h>
#include <osl/generic.h>
#include <osl/extensions/scatnames.h>
#include <osl/extensions/arrays.h>
#include <osl/extensions/extbody.h>

#include <clay/transformation.h>
#include <clay/array.h>
#include <clay/list.h>
#include <clay/macros.h>
#include <clay/options.h>
#include <clay/errors.h>
#include <clay/beta.h>
#include <clay/util.h>


/*****************************************************************************\
 *                     Loop transformations                                   *
 `****************************************************************************/


/**
 * clay_reorder function:
 * Reorders the statements in the loop
 * \param[in,out] scop
 * \param[in] beta_loop     Loop beta vector
 * \param[in] order         Array to reorder the statements
 * \param[in] options
 * \return                  Status
 */
int clay_reorder(osl_scop_p scop, 
                 clay_array_p beta_loop, clay_array_p neworder,
                 clay_options_p options) {

  /* Description:
   * Modify the beta in function of the values in the neworder array.
   */

  osl_relation_p scattering;
  osl_statement_p statement;
  int precision;
  const int column = beta_loop->size * 2; // alpha column
  int row;
  int index;
  
  // important to be sure that the scop normalized first
  // because we access to the reorder array in function of the beta value
  clay_beta_normalize(scop);
  
  statement = clay_beta_find(scop->statement, beta_loop);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_IS_LOOP(beta_loop, statement);
  
  precision = statement->scattering->precision;
  while (statement != NULL) {
    if (clay_beta_check(statement, beta_loop)) {
    
      scattering = statement->scattering;
      row = clay_util_relation_get_line(scattering, column);
      
      // get the beta value
      index = osl_int_get_si(precision,
                             scattering->m[row][scattering->nb_columns-1]);
      
      if (index >= neworder->size)
        return CLAY_ERROR_REORDER_ARRAY_TOO_SMALL;
      
      osl_int_set_si(precision, 
                     &scattering->m[row][scattering->nb_columns-1],
                     neworder->data[index]);
    }
    statement = statement->next;
  }
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/**
 * clay_reverse function:
 * Reverse the direction of the loop
 * \param[in,out] scop
 * \param[in] beta          Beta vector
 * \param[in] options
 * \return                  Status
 */
int clay_reverse(osl_scop_p scop, clay_array_p beta, unsigned int depth,
                 clay_options_p options) {
 
  /* Description:
   * Oppose the output_dims column at the `depth'th level.
   */
  
  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (depth <= 0) 
    return CLAY_ERROR_DEPTH_OVERFLOW;
    
  osl_relation_p scattering;
  osl_statement_p statement = scop->statement;
  int precision;
  int column = depth*2 - 1; // iterator column
  int i;
  
  statement = clay_beta_find(statement, beta);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_CHECK_DEPTH(beta, depth, statement);

  precision = statement->scattering->precision;
  while (statement != NULL) {
    if (clay_beta_check(statement, beta)) {
      scattering = statement->scattering;
      for(i = 0 ; i < scattering->nb_rows ; i++) {
        osl_int_oppose(precision, 
                       &scattering->m[i][column+1],
                       scattering->m[i][column+1]);
      }
    }
    statement = statement->next;
  }
  
  return CLAY_SUCCESS;
}


/**
 * clay_interchange function:
 * On each statement which belongs to the `beta', the loops that match the
 * `depth_1'th and the `depth_2' are interchanged
 * /!\ If you want to interchange 2 loops, you must give the inner beta loop
 * and not the outer !
 * \param[in,out] scop
 * \param[in] beta          Beta vector (inner loop or statement)
 * \param[in] depth_1       >= 1
 * \param[in] depth_2       >= 1
 * \param[in] pretty        1 or 0 : update the scatnames
 * \param[in] options
 * \return                  Status
 */
int clay_interchange(osl_scop_p scop, 
                      clay_array_p beta, 
                      unsigned int depth_1, unsigned int depth_2,
                      int pretty,
                      clay_options_p options) {
  /* Description:
   * Swap the two output_dims columns.
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (depth_1 <= 0 || depth_2 <= 0) 
    return CLAY_ERROR_DEPTH_OVERFLOW;

  osl_statement_p statement = scop->statement;
  osl_relation_p scattering;
  int precision;
  const int column_1 = depth_1*2 - 1; // iterator column
  const int column_2 = depth_2*2 - 1;
  int i;
  osl_int_t **matrix;
  //int nb_rows;

  statement = clay_beta_find(statement, beta);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;
  if (statement->scattering->nb_output_dims == 1)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  
  CLAY_BETA_CHECK_DEPTH(beta, depth_1, statement);
  CLAY_BETA_CHECK_DEPTH(beta, depth_2, statement);

  // it's not useful to interchange the same line
  if (depth_1 == depth_2)
    return CLAY_SUCCESS;
  
  precision = statement->scattering->precision;
  while (statement != NULL) {
  
    if (clay_beta_check(statement, beta)) {
      scattering = statement->scattering;

      //nb_rows = scattering->nb_rows;
      //if (column_1 >= nb_rows || column_2 >= nb_rows)
      //  return CLAY_DEPTH_OVERFLOW;
      
      // swap the two columns
      matrix = scattering->m;
      for (i = 0 ; i < scattering->nb_rows ; i++)
        osl_int_swap(precision, 
                     &matrix[i][column_1+1],
                     &matrix[i][column_2+1]);
    }
    
    statement = statement->next;
  }

  // swap the two variables
  if (pretty) {
    osl_scatnames_p scat;
    osl_strings_p names;
    scat = osl_generic_lookup(scop->extension, OSL_URI_SCATNAMES);
    names = scat->names;

    int ii = 0;
    for (ii = 0 ; names->string[ii] ; ii++)
      ;

    int c1 = depth_1 * 2 - 1;
    int c2 = depth_2 * 2 - 1;

    if (c1 < ii && c2 < ii) {
      char *tmp = names->string[c1];
      names->string[c1] = names->string[c2];
      names->string[c2] = tmp;
    }
  }

  return CLAY_SUCCESS;
}


/**
 * clay_split function:
 * Split the loop in two parts at the `depth'th level from the statement
 * \param[in,out] scop
 * \param[in] beta          Beta vector
 * \param[in] depth         >= 1
 * \param[in] options
 * \return                  Status
 */
int clay_split(osl_scop_p scop, clay_array_p beta, unsigned int depth,
               clay_options_p options) {

  /* Description:
   * Add one on the beta at the `depth'th level.
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (beta->size <= 1 || depth <= 0 || depth >= beta->size)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  
  osl_statement_p statement = scop->statement;
  statement = clay_beta_find(statement, beta);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;
  
  clay_beta_shift_after(scop->statement, beta, depth);
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/**
 * clay_fuse function:
 * Fuse loop with the first loop after
 * \param[in,out] scop
 * \param[in] beta_loop     Loop beta vector
 * \param[in] options
 * \return                  Status
 */
int clay_fuse(osl_scop_p scop, clay_array_p beta_loop,
              clay_options_p options) {

  /* Description:
   * Set the same beta (only at the level of the beta_loop) on the next loop.
   */

  if (beta_loop->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
 
  osl_relation_p scattering;
  osl_statement_p statement;
  clay_array_p beta_max;
  clay_array_p beta_next;
  int precision;
  const int depth = beta_loop->size;
  const int column = beta_loop->size*2; // alpha column
  int row;
  
  statement = clay_beta_find(scop->statement, beta_loop);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_IS_LOOP(beta_loop, statement);

  precision = statement->scattering->precision;
  
  beta_max = clay_beta_max(statement, beta_loop);
  beta_next = clay_beta_next_part(scop->statement, beta_loop);
  
  if (beta_next != NULL) {
    beta_next->size = depth;
    statement = scop->statement;
    while (statement != NULL) {
      if (clay_beta_check(statement, beta_next)) {
        scattering = statement->scattering;
        if (column < scattering->nb_output_dims) {
        
          // Set the loop level
          row = clay_util_relation_get_line(scattering, column-2);
          osl_int_set_si(precision, 
                         &scattering->m[row][scattering->nb_columns-1],
                         beta_loop->data[depth-1]);

          // Reorder the statement
          row = clay_util_relation_get_line(scattering, column);
          osl_int_add_si(precision,
                         &scattering->m[row][scattering->nb_columns-1],
                         scattering->m[row][scattering->nb_columns-1],
                         beta_max->data[depth]+1);
        }
      }
      statement = statement->next;
    }
    clay_array_free(beta_next);
  }
  
  clay_array_free(beta_max);
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/**
 * clay_skew function:
 * Skew the loop (or statement) from the `depth'th loop
 * (i, j) -> (i, j+i*coeff) where `depth' is the loop of i
 * \param[in,out] scop
 * \param[in] beta          Beta vector
 * \param[in] depth         >= 1
 * \param[in] coeff         != 0
 * \param[in] options
 * \return                  Status
 */
int clay_skew(osl_scop_p scop, 
              clay_array_p beta, unsigned int depth, unsigned int coeff,
              clay_options_p options) {

  /* Description:
   * This is a special case of shifting, where params and constant
   * are equal to zero
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (depth <= 0 || depth > beta->size)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  if (coeff == 0)
    return CLAY_ERROR_WRONG_COEFF;

  clay_list_p vector;
  int i, ret;

  // create the vector
  vector = clay_list_malloc();

  // empty arrays
  clay_list_add(vector, clay_array_malloc());
  clay_list_add(vector, clay_array_malloc());
  clay_list_add(vector, clay_array_malloc());

  // set output dims
  for (i = 0 ; i < depth-1 ; i++)
    clay_array_add(vector->data[0], 0);
  clay_array_add(vector->data[0], coeff);

  ret = clay_shift(scop, beta, depth, vector, options);
  clay_list_free(vector);

  return ret;
}


/**
 * clay_iss function:
 * Split the loop (or statement) depending of an inequation.
 * Warning: in the output part, don't put the alpha columns
 *          example: if the output dims are : 0 i 0 j 0, just do Ni, Nj
 * \param[in,out] scop
 * \param[in]  beta_loop         Beta loop
 * \param[in]  inequation array  {(([output, ...],) [param, ...],) [const]}
 * \param[out] beta_max          If NULL, the beta_max will not be returned
 *                               This the last beta which has the prefix
 *                               beta_loop.
 * \param[in]  options
 * \return                      Status
 */
int clay_iss(osl_scop_p scop, 
             clay_array_p beta_loop, clay_list_p inequ,
             clay_array_p *ret_beta_max,
             clay_options_p options) {

  /* Description:
   * Add the inequality for each statements corresponding to the beta.
   * Clone each statements and add the beta on the last beta value
   */

  if (beta_loop->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (inequ->size == 0)
    return CLAY_SUCCESS;
  if (inequ->size > 3)
    return CLAY_ERROR_INEQU;
  if (inequ->size == 0)
    return CLAY_SUCCESS;

  osl_statement_p statement, newstatement;
  osl_relation_p scattering;
  clay_array_p beta_max;
  const int column = beta_loop->size*2; // beta column
  int nb_output_dims, nb_parameters;
  int order; // new loop order for the clones
  
  // search a statement
  statement = clay_beta_find(scop->statement, beta_loop);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;
  if (statement->scattering->nb_input_dims == 0)
    return CLAY_ERROR_BETA_NOT_IN_A_LOOP;

  CLAY_BETA_IS_LOOP(beta_loop, statement);
  
  // decompose the inequation
  nb_parameters = scop->context->nb_parameters;
  nb_output_dims = statement->scattering->nb_output_dims;

  if (inequ->size == 3) {
    // pad zeros
    clay_util_array_output_dims_pad_zero(inequ->data[0]);

    if (inequ->data[0]->size > nb_output_dims ||
        inequ->data[1]->size > nb_parameters ||
        inequ->data[2]->size > 1)
      return CLAY_ERROR_INEQU;

  } else if (inequ->size == 2) {
    if (inequ->data[0]->size > nb_parameters ||
        inequ->data[1]->size > 1)
      return CLAY_ERROR_INEQU;

  } else if (inequ->size == 1) {
    if (inequ->data[0]->size > 1)
      return CLAY_ERROR_INEQU;
  }

  // set the beginning of 'order'
  beta_max = clay_beta_max(statement, beta_loop);
  order = beta_max->data[beta_loop->size] + 1;
  if (ret_beta_max == NULL) {
    clay_array_free(beta_max);
  } else {
    *ret_beta_max = beta_max;
  }

  // insert the inequation on each statements
  while (statement != NULL) {
    if (clay_beta_check(statement, beta_loop)) {

      // insert the inequation
      clay_util_statement_set_inequation(statement, inequ);

      // clone and negate the inequation
      newstatement = osl_statement_nclone(statement, 1);
      scattering = newstatement->scattering;
      clay_util_relation_negate_row(scattering, scattering->nb_rows-1);

      statement = clay_util_statement_insert(statement, newstatement,
                                             column, order);
      order++;
    }
    statement = statement->next;
  }

  if (options && options->normalize)
    clay_beta_normalize(scop);
    
  return CLAY_SUCCESS;
}


/**
 * clay_stripmine function:
 * Decompose a single loop into two nested loop
 * \param[in,out] scop
 * \param[in] beta          Beta vector (loop or statement)
 * \param[in] size          Block size of the inner loop
 * \param[in] pretty        If true, clay will keep the variables name
 * \param[in] options
 * \return                  Status
 */
int clay_stripmine(osl_scop_p scop, clay_array_p beta, 
                   unsigned int depth, unsigned int size, 
                   int pretty, clay_options_p options) {
  
  /* Description:
   * Add two inequality for the stripmining.
   * The new dependance with the loop is done on an output_dim.
   * If pretty is true, we add for each statements (not only the beta) two
   * output_dims (one for the iterator and one for the 2*n+1).
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (size <= 0)
    return CLAY_ERROR_WRONG_BLOCK_SIZE;
  if (depth <= 0)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  
  osl_relation_p scattering;
  osl_statement_p statement = scop->statement;
  osl_scatnames_p scat;
  osl_strings_p names;
  int column = (depth-1)*2; // alpha column
  int precision;
  int row, row_next;
  int i;
  int nb_strings;
  char buffer[OSL_MAX_STRING];
  char *new_var_iter;
  char *new_var_beta;
  
  statement = clay_beta_find(statement, beta);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;
  if (statement->scattering->nb_output_dims < 3)
    return CLAY_ERROR_BETA_NOT_IN_A_LOOP;
  
  CLAY_BETA_CHECK_DEPTH(beta, depth, statement);
  
  precision = statement->scattering->precision;
  if (pretty)
    statement = scop->statement; // TODO optimization...
        
  while (statement != NULL) {
    scattering = statement->scattering;
    if (clay_beta_check(statement, beta)) {
      
      // set the strip mine
      row = clay_util_relation_get_line(scattering, column);
      
      osl_relation_insert_blank_column(scattering, column+1);
      osl_relation_insert_blank_column(scattering, column+1);

      osl_relation_insert_blank_row(scattering, column);
      osl_relation_insert_blank_row(scattering, column);
      osl_relation_insert_blank_row(scattering, column);
      
      // stripmine
      osl_int_set_si(precision, &scattering->m[row+0][column+1], -1);
      osl_int_set_si(precision, &scattering->m[row+1][column+2], -size);
      osl_int_set_si(precision, &scattering->m[row+2][column+2], size);
      osl_int_set_si(precision, 
                     &scattering->m[row+2][scattering->nb_columns-1],
                     size-1);
      
      // inequality
      osl_int_set_si(precision, &scattering->m[row+1][0], 1);
      osl_int_set_si(precision, &scattering->m[row+2][0], 1);
      
      // iterator dependance
      osl_int_set_si(precision, &scattering->m[row+1][column+4], 1);
      osl_int_set_si(precision, &scattering->m[row+2][column+4], -1);
      
      scattering->nb_output_dims += 2;
      
      // reorder
      row_next = clay_util_relation_get_line(scattering, column+2);
      osl_int_assign(precision, 
                     &scattering->m[row][scattering->nb_columns-1],
                     scattering->m[row_next][scattering->nb_columns-1]);
      
      osl_int_set_si(precision, 
                     &scattering->m[row_next][scattering->nb_columns-1],
                     0);
    
    } else if (pretty && column < scattering->nb_output_dims) {
      // add 2 empty dimensions
      row = clay_util_relation_get_line(scattering, column);
      
      osl_relation_insert_blank_column(scattering, column+1);
      osl_relation_insert_blank_column(scattering, column+1);

      osl_relation_insert_blank_row(scattering, column);
      osl_relation_insert_blank_row(scattering, column);
      
      // -Identity
      osl_int_set_si(precision, &scattering->m[row][column+1], -1);
      osl_int_set_si(precision, &scattering->m[row+1][column+2], -1);
      
      scattering->nb_output_dims += 2;
      
      // reorder
      row_next = clay_util_relation_get_line(scattering, column+2);
      osl_int_assign(precision, 
                     &scattering->m[row][scattering->nb_columns-1],
                     scattering->m[row_next][scattering->nb_columns-1]);
      
      osl_int_set_si(precision, 
                     &scattering->m[row_next][scattering->nb_columns-1],
                     0);
    }
    statement = statement->next;
  }
  
  // get the list of scatnames
  scat = osl_generic_lookup(scop->extension, OSL_URI_SCATNAMES);
  names = scat->names;
  
  // generate iterator variable name
  i = 0;
  do {
    sprintf(buffer, "__%s%s%d", names->string[column+1],
            names->string[column+1], i);
    i++;
  } while (clay_util_scatnames_exists(scat, buffer));
  new_var_iter = strdup(buffer);
  
  // generate beta variable name
  i = 0;
  do {
    sprintf(buffer, "__b%d", i);
    i++;
  } while (clay_util_scatnames_exists(scat, buffer));
  new_var_beta = strdup(buffer);
  
  // insert the two variables
  nb_strings = osl_strings_size(names) + 2;
  osl_strings_p newnames = osl_strings_malloc();
  CLAY_malloc(newnames->string, char**, sizeof(char**) * (nb_strings + 1));
  
  for (i = 0 ; i < column ; i++)
    newnames->string[i] = names->string[i];
  
  newnames->string[i] = new_var_beta;
  newnames->string[i+1] = new_var_iter;
  
  for (i = i+2 ; i < nb_strings ; i++)
    newnames->string[i] = names->string[i-2];
  
  newnames->string[i] = NULL; // end of the list
  
  // replace the scatnames
  free(names->string);
  free(names);
  scat->names = newnames;
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/** clay_unroll function: Unroll a loop \param[in,out] scop \param[in]
 * beta_loop     Loop beta vector \param[in] factor        > 0 \param[in]
 * setepilog     if true the epilog will be added \param[in] options \return
 * Status
 */
int clay_unroll(osl_scop_p scop, clay_array_p beta_loop, unsigned int factor,
                int setepilog, clay_options_p options) {

  /* Description:
   * Clone statements in the beta_loop, and recreate the body.
   * Generate an epilog where the lower bound was removed.
   */

  if (beta_loop->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (factor < 1)
    return CLAY_ERROR_WRONG_FACTOR;
  if (factor == 1)
    return CLAY_SUCCESS;
  
  osl_relation_p scattering;
  osl_relation_p domain;
  osl_relation_p epilog_domain = NULL;
  osl_statement_p statement;
  osl_statement_p newstatement;
  osl_statement_p original_stmt;
  osl_statement_p epilog_stmt;
  clay_array_p beta_max;
  int column = beta_loop->size*2;
  int precision;
  int row;
  int nb_stmts;
  int i;
  int max; // last value of beta_max
  int order;
  int order_epilog = 0; // order juste after the beta_loop
  int current_stmt = 0; // counter of statements
  int last_level = -1;
  int current_level;
  
  osl_body_p body;
  osl_body_p newbody;
  char *expression;
  char **iterator;
  char *substitued;
  char *newexpression;
  char *replacement;
  char substitution[] = "@0@";
  
  int iterator_index = beta_loop->size-1;
  int iterator_size;

  int is_extbody;
  
  statement = clay_beta_find(scop->statement, beta_loop);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_IS_LOOP(beta_loop, statement);
  
  precision = statement->scattering->precision;
  
  // alloc an array of string, wich will contain the current iterator and NULL
  // used for osl_util_identifier_substitution with only one variable
  CLAY_malloc(iterator, char**, sizeof(char*)*2);
  iterator[1] = NULL;
  
  // infos used to reorder cloned statements
  nb_stmts = clay_beta_nb_parts(statement, beta_loop);
  beta_max = clay_beta_max(statement, beta_loop);
  max = beta_max->data[beta_max->size-1];
  clay_array_free(beta_max);
  
  if (setepilog) {
    // shift to let the place for the epilog loop
    clay_beta_shift_after(scop->statement, beta_loop, beta_loop->size);
    order_epilog = beta_loop->data[beta_loop->size-1] + 1;
  }
      
  while (statement != NULL) {
    scattering = statement->scattering;
    
    if (clay_beta_check(statement, beta_loop)) {
      
      // create the body with symbols for the substitution
      original_stmt = statement;

      is_extbody = osl_generic_has_URI(statement->body, OSL_URI_EXTBODY);
      body = is_extbody ?
             ((osl_extbody_p) statement->body->data)->body :
             (osl_body_p) statement->body->data;

      expression = body->expression->string[0];
      iterator[0] = (char*) body->iterators->string[iterator_index];
      iterator_size = strlen(iterator[0]);
      substitued = osl_util_identifier_substitution(expression, iterator);
      
      CLAY_malloc(replacement, char*, 1 + iterator_size + 1 + 16 + 1 + 1);
      
      if (setepilog) {
        // set the epilog from the original statement
        epilog_stmt = osl_statement_nclone(original_stmt, 1);
        row = clay_util_relation_get_line(original_stmt->scattering, column-2);
        scattering = epilog_stmt->scattering;
        osl_int_set_si(precision,
                       &scattering->m[row][scattering->nb_columns-1],
                       order_epilog);
        
        // the order is not important in the statements list
        epilog_stmt->next = statement->next;
        statement->next = epilog_stmt;
        statement = epilog_stmt;
      }
      
      // modify the matrix domain
      domain = original_stmt->domain;
      
      if (setepilog) {
        epilog_domain = epilog_stmt->domain;
      }
      
      while (domain != NULL) {
      
        for (i = domain->nb_rows-1 ; i >= 0  ; i--) {
          if (!osl_int_zero(precision, domain->m[i][0])) {
          
            // remove the lower bound on the epilog statement
            if(setepilog &&
               osl_int_pos(precision, domain->m[i][iterator_index+1])) {
              osl_relation_remove_row(epilog_domain, i);
            }
            // remove the upper bound on the original statement
            if (osl_int_neg(precision, domain->m[i][iterator_index+1])) {
              osl_int_add_si(precision, 
                             &domain->m[i][domain->nb_columns-1],
                             domain->m[i][domain->nb_columns-1],
                             -factor);
            }
          }
        }
        
        // add local dim on the original statement
        osl_relation_insert_blank_column(domain, domain->nb_output_dims+1);
        osl_relation_insert_blank_row(domain, 0);
        (domain->nb_local_dims)++;
        osl_int_set_si(precision, 
                       &domain->m[0][domain->nb_output_dims+1],
                       -factor);
        osl_int_set_si(precision, &domain->m[0][iterator_index+1], 1);
        
        domain = domain->next;
        
        if (setepilog)
          epilog_domain = epilog_domain->next;
      }
      
      // clone factor-1 times the original statement
     
      row = clay_util_relation_get_line(original_stmt->scattering, column);
      current_level = osl_int_get_si(scattering->precision,
                                 scattering->m[row][scattering->nb_columns-1]);
      if (last_level != current_level) {
        current_stmt++;
        last_level = current_level;
      }
      
      for (i = 0 ; i < factor-1 ; i++) {
        newstatement = osl_statement_nclone(original_stmt, 1);
        scattering = newstatement->scattering;
        
        // update the body
        sprintf(replacement, "(%s+%d)", iterator[0], i+1);
        newexpression = clay_util_string_replace(substitution, replacement,
                                            substitued);

        newbody = is_extbody ?
                  ((osl_extbody_p) newstatement->body->data)->body :
                  (osl_body_p) newstatement->body->data;

        free(newbody->expression->string[0]);
        newbody->expression->string[0] = newexpression;
        
        // reorder
        order = current_stmt + max + nb_stmts*i;
        osl_int_set_si(precision,
                       &scattering->m[row][scattering->nb_columns-1],
                       order);
        
        // the order is not important in the statements list
        newstatement->next = statement->next;
        statement->next = newstatement;
        statement = newstatement;
      }
      free(substitued);
      free(replacement);
    }
    statement = statement->next;
  }
  free(iterator);
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/**
 * clay_tile function:
 * Apply a stripmine then an interchange.
 * The stripmine is at the `depth'th level. Next interchange the level `depth'
 * and `depth_outer'
 * \param[in,out] scop
 * \param[in] beta          Beta vector
 * \param[in] depth         >=1
 * \param[in] size          >=1
 * \param[in] depth_outer   >=1
 * \param[in] pretty        See stripmine
 * \param[in] options
 * \return                  Status
 */
int clay_tile(osl_scop_p scop, 
              clay_array_p beta, unsigned int depth, unsigned int depth_outer,
              unsigned int size, int pretty, clay_options_p options) {

  /* Description:
   * stripmine + interchange
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (size <= 0)
    return CLAY_ERROR_WRONG_BLOCK_SIZE;
  if (depth <= 0 || depth_outer <= 0)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  if (depth_outer > depth)
    return CLAY_ERROR_DEPTH_OUTER;
  
  int ret;
  ret = clay_stripmine(scop, beta, depth, size, pretty, options);
  
  if (ret == CLAY_SUCCESS) {
    ret = clay_interchange(scop, beta, depth, depth_outer, pretty, options);
  }

  return ret;
}


/**
 * clay_shift function:
 * Shift the iteration domain
 * Warning: in the output part, don't put the alpha columns
 *          example: if the output dims are : 0 i 0 j 0, just do Ni, Nj
 * \param[in,out] scop
 * \param[in] beta          Beta vector (loop or statement)
 * \param[in] depth         >=1
 * \param[in] vector        {(([output, ...],) [param, ...],) [const]}
 * \param[in] options
 * \return                  Status
 */
int clay_shift(osl_scop_p scop, 
               clay_array_p beta, unsigned int depth, clay_list_p vector,
               clay_options_p options) {

  /* Description:
   * Add a vector on each statements.
   */

  if (beta->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (vector->size == 0)
    return CLAY_ERROR_VECTOR_EMPTY;
  if (depth <= 0)
    return CLAY_ERROR_DEPTH_OVERFLOW;
  if (vector->size > 3)
    return CLAY_ERROR_VECTOR;
  if (vector->size == 0)
    return CLAY_SUCCESS;
  
  osl_statement_p statement;
  const int column = depth*2 - 1; // iterator column
  int nb_parameters, nb_output_dims;
  
  statement = clay_beta_find(scop->statement, beta);
  if (!statement)
    return CLAY_ERROR_BETA_NOT_FOUND;
  if (statement->scattering->nb_input_dims == 0)
    return CLAY_ERROR_BETA_NOT_IN_A_LOOP;

  CLAY_BETA_CHECK_DEPTH(beta, depth, statement);

  // decompose the vector
  nb_parameters = scop->context->nb_parameters;
  nb_output_dims = statement->scattering->nb_output_dims;

  if (vector->size == 3) {
    // pad zeros
    clay_util_array_output_dims_pad_zero(vector->data[0]);

    if (vector->data[0]->size > nb_output_dims ||
        vector->data[1]->size > nb_parameters ||
        vector->data[2]->size > 1)
      return CLAY_ERROR_VECTOR;

  } else if (vector->size == 2) {
    if (vector->data[0]->size > nb_parameters ||
        vector->data[1]->size > 1)
      return CLAY_ERROR_VECTOR;

  } else if (vector->size == 1) {
    if (vector->data[0]->size > 1)
      return CLAY_ERROR_VECTOR;
  }

  // add the vector for each statements
  while (statement != NULL) {
    if (clay_beta_check(statement, beta))
      clay_util_statement_set_vector(statement, vector, column);
    statement = statement->next;
  }
  
  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return CLAY_SUCCESS;
}


/**
 * clay_peel function:
 * Removes the first or last iteration of the loop into separate code.
 * This is equivalent to an iss (without output dims) and a spliting
 * \param[in,out] scop
 * \param[in] beta_loop         Loop beta vector
 * \param[in] peeling list      { ([param, ...],) [const] }
 * \param[in] options
 * \return                      Status
 */
int clay_peel(osl_scop_p scop, 
              clay_array_p beta_loop, clay_list_p peeling,
              clay_options_p options) {

  if (beta_loop->size == 0)
    return CLAY_ERROR_BETA_EMPTY;
  if (peeling->size >= 3)
    return CLAY_ERROR_INEQU;
  if (peeling->size == 0)
    return CLAY_SUCCESS;
  
  clay_array_p arr_dims, beta_max;
  clay_list_p new_peeling;
  int i, ret;
  
  // create output dims
  arr_dims = clay_array_malloc();
  for (i = 0 ; i < beta_loop->size-1 ; i++)
    clay_array_add(arr_dims, 0);
  clay_array_add(arr_dims, 1);

  // create new peeling list
  new_peeling = clay_list_malloc();
  clay_list_add(new_peeling, arr_dims);
  if (peeling->size == 2) {
    clay_list_add(new_peeling, peeling->data[0]);
    clay_list_add(new_peeling, peeling->data[1]);
  } else {
    clay_list_add(new_peeling, clay_array_malloc()); // empty params
    clay_list_add(new_peeling, peeling->data[0]);
  }
  
  // index-set spliting
  ret = clay_iss(scop, beta_loop, new_peeling, &beta_max, options);

  // we don't free ALL with clay_list_free, because there are arrays in
  // common with `peeling'
  clay_array_free(arr_dims);
  if (peeling->size == 1)
    clay_array_free(new_peeling->data[1]);
  free(new_peeling->data);
  free(new_peeling);

  if (ret == CLAY_SUCCESS) {
    // see clay_split
    clay_beta_shift_after(scop->statement, beta_max, 
                          beta_loop->size);
  }

  clay_array_free(beta_max);

  if (options && options->normalize)
    clay_beta_normalize(scop);
  
  return ret;
}


/**
 * clay_context function:
 * Add a line to the context
 * \param[in,out] scop
 * \param[in] vector        [param1, param2, ..., 1]
 * \param[in] options
 * \return                  Status
 */
int clay_context(osl_scop_p scop, clay_array_p vector, 
                 clay_options_p options) {

  /* Description:
   * Add a new line in the context matrix.
   */

  if (vector->size < 2)
    return CLAY_ERROR_VECTOR;
  if (scop->context->nb_parameters != vector->size-2)
    return CLAY_ERROR_VECTOR;
  
  osl_relation_p context;
  int row;
  int i, j;
  int precision;
  
  context = scop->context;
  precision = context->precision;
  row = context->nb_rows;
  osl_relation_insert_blank_row(context, row);
  
  osl_int_set_si(precision,
                 &context->m[row][0],
                 vector->data[0]);
  
  j = 1 + context->nb_output_dims + context->nb_input_dims +
      context->nb_local_dims;
  for (i = 1 ; i < vector->size ; i++) {
    osl_int_set_si(precision,
                   &context->m[row][j],
                   vector->data[i]);
    j++;
  }
  
  return CLAY_SUCCESS;
}


static int clay_dimreorder_aux(osl_relation_list_p access,
                               void *args) {
  clay_array_p neworder = args;
  osl_relation_p a = access->elt;

  if (a->nb_output_dims-1 != neworder->size) {
    fprintf(stderr, "[Clay] Warning: can't reorder dims on this statement: ");
    return CLAY_ERROR_REORDER_ARRAY_SIZE;
  }

  osl_relation_p tmp = osl_relation_nclone(a, 1);
  int i, j;

  for (i = 0 ; i < neworder->size ; i++) {
    if (neworder->data[i] < 0 ||
        neworder->data[i] >= a->nb_output_dims-1)
      return CLAY_ERROR_REORDER_OVERFLOW_VALUE;

    if (i+2 != neworder->data[i]+2)
      for (j = 0 ; j < a->nb_rows ; j++)
        osl_int_assign(a->precision, 
                       &tmp->m[j][i+2],
                       a->m[j][neworder->data[i]+2]);
  }

  osl_relation_free(a);
  access->elt = tmp;

  return CLAY_SUCCESS;
}


/**
 * clay_dimreorder function:
 * Reorder the dimensions of access_ident
 * \param[in,out] scop
 * \param[in] beta
 * \param[in] access_ident   ident of the access array
 * \param[in] neworder      reorder the dims 
 * \param[in] options
 * \return                  Status
 */
int clay_dimreorder(osl_scop_p scop,
                    clay_array_p beta,
                    unsigned int access_ident,
                    clay_array_p neworder,
                    clay_options_p options) {

  /* Description
   * Swap the columns in the output dims (access arrays)
   * The first output dim is not used ( => access name) 
   */

  // core of the function : clay_dimreorder_aux

  return clay_util_foreach_access(scop, beta, access_ident, 
                                  clay_dimreorder_aux, neworder, 1);
}


static int clay_dimprivatize_aux(osl_relation_list_p access, void *args) {
  int depth = *((int*) args);
  osl_relation_p a = access->elt;

  // check if the iterator is not used
  int i;
  for (i = 0 ; i < a->nb_rows ; i++) {
    if (!osl_int_zero(a->precision, a->m[i][a->nb_output_dims + depth])) {
      fprintf(stderr, 
          "[Clay] Warning: can't privatize this statement\n"
          "                the dim (depth=%d) seems to be already used\n"
          "                the depth is the depth in the loops\n"
          "                ", depth);
      return CLAY_ERROR_CANT_PRIVATIZE;
    }
  }

  a->nb_output_dims++;

  osl_relation_insert_blank_column(a, a->nb_output_dims);
  osl_relation_insert_blank_row(a, a->nb_rows);

  osl_int_set_si(a->precision, 
                 &a->m[a->nb_rows-1][a->nb_output_dims],
                 -1);

  osl_int_set_si(a->precision,
                 &a->m[a->nb_rows-1][a->nb_output_dims + depth],
                 1);

  return CLAY_SUCCESS;
}


/**
 * clay_dimprivatize function:
 * Privatize an access
 * \param[in,out] scop
 * \param[in] beta
 * \param[in] access_ident   ident of the access array
 * \param[in] depth
 * \param[in] options
 * \return                  Status
 */
int clay_dimprivatize(osl_scop_p scop,
                      clay_array_p beta,
                      unsigned int access_ident,
                      unsigned int depth,
                      clay_options_p options) {

  /* Description
   * Add an output dim on each access which are in the beta vector
   */

  if (depth <= 0) 
    return CLAY_ERROR_DEPTH_OVERFLOW;

  osl_statement_p stmt = clay_beta_find(scop->statement, beta);
  if (!stmt)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_CHECK_DEPTH(beta, depth, stmt);

  // core of the function : clay_dimprivatize_aux

  return clay_util_foreach_access(scop, beta, access_ident,
                                  clay_dimprivatize_aux, &depth, 1);
}


static int clay_dimcontract_aux(osl_relation_list_p access, void *args) {
  int depth = *((int*) args);
  osl_relation_p a = access->elt;

  int row = clay_util_relation_get_line(a, depth);
  if (row != -1) {
    osl_relation_remove_row(a, row);
    osl_relation_remove_column(a, depth+1); // remove output dim
    a->nb_columns--;
    a->nb_output_dims--;
  }

  return CLAY_SUCCESS;
}


/**
 * clay_dimcontract function:
 * Contract an access (remove a dimension)
 * \param[in,out] scop
 * \param[in] beta
 * \param[in] access_ident   ident of the access array
 * \param[in] depth
 * \param[in] options
 * \return                  Status
 */
int clay_dimcontract(osl_scop_p scop,
                     clay_array_p beta,
                     unsigned int access_ident,
                     unsigned int depth,
                     clay_options_p options) {
  /* Description
   * Remove the line/column at the depth level
   */

  if (depth <= 0) 
    return CLAY_ERROR_DEPTH_OVERFLOW;

  osl_statement_p stmt = clay_beta_find(scop->statement, beta);
  if (!stmt)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_CHECK_DEPTH(beta, depth, stmt);

  // core of the function : clay_dimcontract_aux

  return clay_util_foreach_access(scop, beta, access_ident, 
                                  clay_dimcontract_aux, &depth, 1);
}


/**
 * clay_add_array function:
 * Add a new array in the arrays extensions.
 * \param[in,out] scop
 * \param[in] name          string name
 * \param[in] result        return the new id
 * \param[in] options
 * \return                  Status
 */
int clay_add_array(osl_scop_p scop,
                  char *name,
                  int *result,
                  clay_options_p options) {

  osl_arrays_p arrays = osl_generic_lookup(scop->extension, OSL_URI_ARRAYS);
  if (!arrays)
    return CLAY_ERROR_ARRAYS_EXT_EMPTY;

  int i;
  int sz = arrays->nb_names;

  if (sz == 0)
    return CLAY_SUCCESS;

  for (i = 0 ; i < sz ; i++)
    if (strcmp(arrays->names[i], name) == 0)
      return CLAY_ERROR_ID_EXISTS;

  int id = arrays->id[0];

  for (i = 1 ; i < sz ; i++)
    if (arrays->id[i] > id)
      id = arrays->id[i];

  arrays->nb_names++;

  // I don't know why, there is a valgrind warning when I use CLAY_realloc
  arrays->id = realloc(arrays->id, sizeof(int) * (sz+1));
  arrays->names = realloc(arrays->names, sizeof(char*) * (sz+1));

  id++;
  
  arrays->id[sz] = id;
  arrays->names[sz] = strdup(name);

  *result = id;

  return CLAY_SUCCESS;
}

/**
 * clay_get_array_id function:
 * Search the array name in the arrays extension
 * \param[in,out] scop
 * \param[in] name          string name
 * \param[in] result        return the id
 * \param[in] options
 * \return                  Status
 */
int clay_get_array_id(osl_scop_p scop,
                      char *name,
                      int *result,
                      clay_options_p options) {

  osl_arrays_p arrays = osl_generic_lookup(scop->extension, OSL_URI_ARRAYS);
  if (!arrays)
    return CLAY_ERROR_ARRAYS_EXT_EMPTY;

  int i;
  int sz = arrays->nb_names;
  int id = -1;

  for (i = 0 ; i < sz ; i++)
    if (strcmp(arrays->names[i], name) == 0) {
      id = arrays->id[i];
      break;
    }

  if (id == -1)
    return CLAY_ERROR_ARRAY_NOT_FOUND;

  *result = id;

  return CLAY_SUCCESS;
}


static int clay_replace_array_aux(osl_relation_list_p access, void *args) {
  int new_id = *((int*) args);
  osl_relation_p a = access->elt;

  // here row is != -1, because the function aux shouldn't be called
  int row = clay_util_relation_get_line(a, 0); 
  osl_int_set_si(a->precision, &a->m[row][a->nb_columns-1], new_id);

  return CLAY_SUCCESS;
}

/**
 * clay_replace_array function:
 * Replace an ident array by another in each access
 * \param[in,out] scop
 * \param[in] last_id
 * \param[in] new_id
 * \param[in] options
 * \return    Status
 */
int clay_replace_array(osl_scop_p scop,
                       unsigned int last_id,
                       unsigned int new_id,
                       clay_options_p options) {

  // core of the function : clay_replace_array_aux

  clay_array_p beta = clay_array_malloc();
  int ret = clay_util_foreach_access(scop, beta, last_id,
                                     clay_replace_array_aux, &new_id, 1);
  clay_array_free(beta);

  return ret;
}


/**
 * clay_datacopy function:
 * This function will generate a loop to copy all data from the array
 * `array_id_original' to a new array `array_id_copy'. Use the function
 * add_array to insert a new array in the scop. A domain and a scattering
 * is needed to generate the loop to copy the data. They is a copy
 * from the domain/scattering of the first statement which corresponds
 * to the `beta_get_domain'. There is just a modification on the domain to
 * remove unused loops (iter >= 1 && iter <= -1 is set). The orignal id or
 * the copy id must be in this beta (in the list of access relation). Genarally
 * you will use replace_array before calling datacopy, that's why the
 * array_id_copy can be in the scop.  The first access relation which is found
 * will be used to generate an access for the original id and the copy id.
 * \param[in,out] scop
 * \param[in] array_id_copy     new variable
 * \param[in] array_id_original
 * \param[in] beta             the loop is insert after the beta
 * \param[in] beta_get_domain  domain/scattering are copied from this beta
 *                             the original or copy id must be in the list of 
 *                             access of this beta
 * \param[in] options
 * \return                     Status
 */
int clay_datacopy(osl_scop_p scop,
                  unsigned int array_id_copy,
                  unsigned int array_id_original,
                  clay_array_p beta_insert,
                  int insert_before,
                  clay_array_p beta_get_domain,
                  clay_options_p options) {

  /* Description
   * - search the statement S beta_get_domain
   * - search the array array_id_original or array_id_copy in the list of
   *   access in the given statement
   * - clone the found access array
   * - reclone this access and put the other array id (it depends which
   *   id is found in the second step)
   * - search the beta_insert
   * - shift all the beta after or before beta_insert to let the place of the
   *   new statement
   * - copy domain + scattering of S in a new statement (and add this one in
   *   the scop)
   * - for each unused iterators we put iter >= 1 && iter <= -1 in the domain
   *   to remove the loop at the generation of code
   * - put the 2 access arrays in this statement
   * - generate body
   */

  osl_relation_p scattering;
  int row, i, j;

  // TODO : global vars ??
  osl_arrays_p arrays;
  osl_scatnames_p scatnames;
  osl_strings_p params;
  arrays = osl_generic_lookup(scop->extension, OSL_URI_ARRAYS);
  scatnames = osl_generic_lookup(scop->extension, OSL_URI_SCATNAMES);
  params = osl_generic_lookup(scop->parameters, OSL_URI_STRINGS);

  if (beta_insert->size == 0)
    return CLAY_ERROR_BETA_EMPTY;

  // search the beta where we have to insert the new loop
  osl_statement_p stmt_1 = clay_beta_find(scop->statement, beta_insert);
  if (!stmt_1)
    return CLAY_ERROR_BETA_NOT_FOUND;

  // search the beta which is need to copy the domain and scattering
  osl_statement_p stmt_2 = clay_beta_find(scop->statement, beta_get_domain);
  if (!stmt_2)
    return CLAY_ERROR_BETA_NOT_FOUND;

  // copy the domain/scattering
  osl_statement_p copy = osl_statement_malloc();
  copy->domain = osl_relation_clone(stmt_2->domain);
  copy->scattering = osl_relation_clone(stmt_2->scattering);


  // create an extbody and copy the original iterators

  int is_extbody = osl_generic_has_URI(stmt_2->body, OSL_URI_EXTBODY);
  osl_extbody_p ebody = osl_extbody_malloc();
  ebody->body = osl_body_malloc();

  // body string (it will be regenerated)
  ebody->body->expression = osl_strings_encapsulate(strdup("@ = @;"));

  // copy iterators
  ebody->body->iterators = is_extbody
    ? osl_strings_clone(((osl_extbody_p) stmt_2->body->data)->body->iterators) 
    : osl_strings_clone(((osl_body_p) stmt_2->body->data)->iterators);

  // 2 access coordinates
  osl_extbody_add(ebody, 0, 1);
  osl_extbody_add(ebody, 4, 1);

  copy->body = osl_generic_shell(ebody, osl_extbody_interface());


  // search the array_id in the beta_get_domain
  
  osl_relation_list_p access = stmt_2->access;
  osl_relation_p a;
  int id;

  while (access) {
    a = access->elt;
    id = osl_relation_get_array_id(a);
    if (id == array_id_original || id == array_id_copy)
      break;
    access = access->next;
  }

  if (!access)
    return CLAY_ERROR_ARRAY_NOT_FOUND_IN_THE_BETA;

  // matrix of the copied array
  a = osl_relation_nclone(a, 1);
  a->type = OSL_TYPE_WRITE;
  osl_int_set_si(a->precision, &a->m[0][a->nb_columns-1], array_id_copy);
  copy->access = osl_relation_list_malloc();
  copy->access->elt = a;

  clay_util_body_regenerate_access(ebody, a, 0, arrays, scatnames, params);

  // matrix of the original array
  a = osl_relation_nclone(a, 1);
  a->type = OSL_TYPE_READ;
  copy->access->next = osl_relation_list_malloc();
  copy->access->next->elt = a;
  copy->access->next->next = NULL;
  osl_int_set_si(a->precision, &a->m[0][a->nb_columns-1], array_id_original);

  clay_util_body_regenerate_access(ebody, a, 1, arrays, scatnames, params);


  // remove the unused dim in the scatterin (modify the domain of the loop)
  for (j = 0 ; j < a->nb_input_dims ; j++) {
    int found = 0;

    // search an unused input dims (if there is only 0 on the row)
    for (i = 0 ; i < a->nb_rows ; i++) {
      if (!osl_int_zero(a->precision, a->m[i][1 + a->nb_output_dims + j])) {
        found = 1;
        break;
      }
    }

    // unused
    if (!found) {
      int k, t;
      osl_relation_p domain = copy->domain;
      for (i = 0 ; i < domain->nb_rows ; i++) {
        t = osl_int_get_si(domain->precision, domain->m[i][1 + j]);

        if (t != 0) {
          for (k = 1 ; k < domain->nb_columns-1 ; k++)
            if (k != 1+j)
              osl_int_set_si(domain->precision, &domain->m[i][k], 0);

          // set iter <= -1 && iter >= 1  ==> no solutions, so no loop !
          if (t > 0)
            osl_int_set_si(domain->precision, &domain->m[i][k], -1);
          else
            osl_int_set_si(domain->precision, &domain->m[i][k], 1);
        }
      }
    }
  }


  // let the place to the new loop

  scattering = copy->scattering;
  if (insert_before) {
    clay_beta_shift_before(scop->statement, beta_insert, 1);

    // set the beta : it's the beta[0]
    row = clay_util_relation_get_line(scattering, 0);
    osl_int_set_si(scattering->precision, 
                   &scattering->m[row][scattering->nb_columns-1],
                   beta_insert->data[0]);
  } else {
    clay_beta_shift_after(scop->statement, beta_insert, 1);

    // set the beta : it's the beta[0] + 1
    row = clay_util_relation_get_line(scattering, 0);
    osl_int_set_si(scattering->precision, 
                   &scattering->m[row][scattering->nb_columns-1],
                   beta_insert->data[0]+1);
  }

  copy->next = scop->statement;
  scop->statement = copy;

  if (options && options->normalize)
    clay_beta_normalize(scop); 

  return CLAY_SUCCESS;
}


/**
 * clay_block function:
 * \param[in,out] scop
 * \param[in] beta_stmt1
 * \param[in] beta_stmt2
 * \param[in] options
 * \return                     Status
 */
int clay_block(osl_scop_p scop,
               clay_array_p beta_stmt1,
               clay_array_p beta_stmt2,
               clay_options_p options) {

  /* Description
   * concat '{' + body(stmt_1) + body(stmt_2) + '}'
   * update extbody if needed
   * concat access(stmt_1) + access(stmt_2)
   * remove stmt_2
   */

  if (beta_stmt1->size != beta_stmt2->size)
    return CLAY_ERROR_BETAS_NOT_SAME_DIMS;

  // search statements and check betas

  osl_statement_p stmt_1 = clay_beta_find(scop->statement, beta_stmt1);
  if (!stmt_1)
    return CLAY_ERROR_BETA_NOT_FOUND;

  osl_statement_p stmt_2 = clay_beta_find(scop->statement, beta_stmt2);
  if (!stmt_2)
    return CLAY_ERROR_BETA_NOT_FOUND;

  CLAY_BETA_IS_STMT(beta_stmt1, stmt_1);
  CLAY_BETA_IS_STMT(beta_stmt2, stmt_2);

  if (!osl_relation_equal(stmt_1->domain, stmt_2->domain))
    return CLAY_ERROR_BETAS_NOT_SAME_DOMAIN;

  int i;
  int is_extbody_1, is_extbody_2;

  char **expr_left;
  char **expr_right;
  char *new_expr;

  osl_extbody_p ebody_1, ebody_2;

  // get the body string

  is_extbody_1 = osl_generic_has_URI(stmt_1->body, OSL_URI_EXTBODY);
  expr_left = is_extbody_1
    ? ((osl_extbody_p) stmt_1->body->data)->body->expression->string
    : ((osl_body_p) stmt_1->body->data)->expression->string;

  is_extbody_2 = osl_generic_has_URI(stmt_2->body, OSL_URI_EXTBODY);
  expr_right = is_extbody_2
    ? ((osl_extbody_p) stmt_2->body->data)->body->expression->string
    : ((osl_body_p) stmt_2->body->data)->expression->string;

  if (is_extbody_1 != is_extbody_2)
    return CLAY_ERROR_ONE_HAS_EXTBODY;

  // update extbody
  if (is_extbody_1) {
    ebody_1 = stmt_1->body->data;
    ebody_2 = stmt_2->body->data;

    // shift for the '{'
    for (i = 0 ; i < ebody_1->nb_access ; i++) {
      if (ebody_1->start[i] != -1)
        ebody_1->start[i]++;
    }

    int offset = 1 + strlen(expr_left[0]);

    // shift the right part
    for (i = 0 ; i < ebody_2->nb_access ; i++) {
      if (ebody_2->start[i] != -1)
        ebody_2->start[i] += offset;

      // concat with the extbody 2
      osl_extbody_add(ebody_1, ebody_2->start[i], ebody_2->length[i]);
    }

  }

  // generate the new body string
  new_expr = (char*) malloc(strlen(expr_left[0]) + strlen(expr_right[0]) + 3);
  strcpy(new_expr, "{");
  strcat(new_expr, expr_left[0]);
  strcat(new_expr, expr_right[0]);
  strcat(new_expr, "}");

  free(expr_left[0]);
  expr_left[0] = new_expr;

  // concat all the access array
  osl_relation_list_p access = stmt_1->access;
  if (access) {
    while (access->next)
      access = access->next;
  }
  access->next = stmt_2->access;
  stmt_2->access = NULL;

  // search the stmt 2 and remove it in the chain list
  osl_statement_p s = scop->statement;
  if (s) {
    while (s->next) {
      if (s->next == stmt_2) {
        s->next = s->next->next;
        osl_statement_free(stmt_2);
        break;
      }
      s = s->next;
    }
  }

  if (options && options->normalize)
    clay_beta_normalize(scop); 

  return CLAY_SUCCESS;
}
