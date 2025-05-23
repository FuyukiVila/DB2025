/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL
v2. You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name,
                   std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids,
                   Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
    std::unique_ptr<RmRecord> Next() override {
        // Todo:
        // !需要自己实现
        for (auto &rid : rids_) {
            // 获取旧记录
            auto old_rec = fh_->get_record(rid, context_);
            auto new_rec = std::make_unique<RmRecord>(*old_rec);

            // 更新索引：先删除旧的索引项
            for (auto &index : tab_.indexes) {
                auto ih = sm_manager_->ihs_
                              .at(sm_manager_->get_ix_manager()->get_index_name(
                                  tab_name_, index.cols))
                              .get();
                char *old_key = new char[index.col_tot_len];
                int offset = 0;
                for (size_t i = 0; i < index.col_num; ++i) {
                    memcpy(old_key + offset,
                           old_rec->data + index.cols[i].offset,
                           index.cols[i].len);
                    offset += index.cols[i].len;
                }
                ih->delete_entry(old_key, context_->txn_);
                delete[] old_key;
            }

            // 应用更新
            for (auto &set_clause : set_clauses_) {
                auto col = tab_.get_col(set_clause.lhs.col_name);
                if (col->type != set_clause.rhs.type) {
                    throw IncompatibleTypeError(
                        coltype2str(col->type),
                        coltype2str(set_clause.rhs.type));
                }
                set_clause.rhs.init_raw(col->len);
                memcpy(new_rec->data + col->offset, set_clause.rhs.raw->data,
                       col->len);
            }

            // 更新记录
            fh_->update_record(rid, new_rec->data, context_);

            // 插入新的索引项
            for (auto &index : tab_.indexes) {
                auto ih = sm_manager_->ihs_
                              .at(sm_manager_->get_ix_manager()->get_index_name(
                                  tab_name_, index.cols))
                              .get();
                char *new_key = new char[index.col_tot_len];
                int offset = 0;
                for (size_t i = 0; i < index.col_num; ++i) {
                    memcpy(new_key + offset,
                           new_rec->data + index.cols[i].offset,
                           index.cols[i].len);
                    offset += index.cols[i].len;
                }
                ih->insert_entry(new_key, rid, context_->txn_);
                delete[] new_key;
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};