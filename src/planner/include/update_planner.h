#pragma once

#include "src/binder/query/updating_clause/include/bound_create_clause.h"
#include "src/binder/query/updating_clause/include/bound_delete_clause.h"
#include "src/binder/query/updating_clause/include/bound_set_clause.h"
#include "src/binder/query/updating_clause/include/bound_updating_clause.h"
#include "src/planner/logical_plan/include/logical_plan.h"

namespace kuzu {
namespace planner {

class UpdatePlanner {
public:
    UpdatePlanner() = default;

    inline void planUpdatingClause(
        BoundUpdatingClause& updatingClause, vector<unique_ptr<LogicalPlan>>& plans) {
        for (auto& plan : plans) {
            planUpdatingClause(updatingClause, *plan);
        }
    }

private:
    void planUpdatingClause(BoundUpdatingClause& updatingClause, LogicalPlan& plan);
    void planSetItem(expression_pair setItem, LogicalPlan& plan);

    void planCreate(BoundCreateClause& createClause, LogicalPlan& plan);
    void appendCreateNode(
        const vector<unique_ptr<BoundCreateNode>>& createNodes, LogicalPlan& plan);
    void appendCreateRel(const vector<unique_ptr<BoundCreateRel>>& createRels, LogicalPlan& plan);

    inline void appendSet(BoundSetClause& setClause, LogicalPlan& plan) {
        appendSet(setClause.getSetItems(), plan);
    }
    void appendSet(vector<expression_pair> setItems, LogicalPlan& plan);

    void planDelete(BoundDeleteClause& deleteClause, LogicalPlan& plan);
    void appendDeleteNode(
        const vector<unique_ptr<BoundDeleteNode>>& deleteNodes, LogicalPlan& plan);
    void appendDeleteRel(const vector<shared_ptr<RelExpression>>& deleteRels, LogicalPlan& plan);
};

} // namespace planner
} // namespace kuzu
