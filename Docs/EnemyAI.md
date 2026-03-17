# Enemy NPC AI Setup

## Added C++ classes

- `AEnemyCharacter`
- `AEnemyAIController`
- `UBTService_UpdateTarget`
- `UBTTask_FindPatrolLocation`
- `UBTTask_PerformMeleeAttack`

## Blackboard

Create a Blackboard asset and add these keys:

- `TargetActor` : `Object` (`Actor`)
- `PatrolLocation` : `Vector`
- `SpawnLocation` : `Vector`
- `IsInAttackRange` : `Bool`

## Behavior Tree

Create a Behavior Tree that uses the Blackboard above.

Recommended tree:

1. Root
2. Selector
3. Sequence `ChaseAndAttack`
4. Service on `ChaseAndAttack` : `Update Target`
5. Decorator on `ChaseAndAttack` : Blackboard `TargetActor Is Set`
6. `Move To`
7. Blackboard Key for `Move To` : `TargetActor`
8. `Perform Melee Attack`
9. Sequence `Patrol`
10. `Find Patrol Location`
11. `Move To`
12. Blackboard Key for second `Move To` : `PatrolLocation`

Optional improvement:

- Add a Blackboard decorator before `Perform Melee Attack` using `IsInAttackRange == true`.

## Blueprint setup

1. Create `BP_EnemyCharacter` from `AEnemyCharacter`.
2. Assign the Behavior Tree asset to `BehaviorTreeAsset`.
3. Place the enemy in the level.
4. Make sure the map has a NavMesh Bounds Volume.
5. Press `P` in the editor and verify the navigation area is green.

## Tunable values on `AEnemyCharacter`

- `PatrolRadius`
- `DetectionRadius`
- `LoseTargetRadius`
- `AttackRange`
- `AttackDamage`

## Current behavior

- No target: enemy patrols near spawn.
- Player detected: enemy sets the player as target and chases.
- In attack range: enemy applies damage to the player character.
- Target lost: enemy clears the target and returns to patrol.
