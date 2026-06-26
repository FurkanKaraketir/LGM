# Multiple Normal Tree Finding

How to enumerate **all normal trees** (minimum spanning trees under branch-type weights) for a linear graph. This walkthrough follows Altun, Kerem; Balkan, R. Tuna; & Platin, Bülent (2002); see **Help → About** for the full citation.

After enumeration, each tree can be passed to the state-equation derivation described in **State-Space Derivation**.

---

## Branch weights

Assign an integer weight to every branch from its element type:

| Element type | Weight |
|--------------|--------|
| A-type active | 1 |
| A-type passive | 2 |
| D-type | 3 |
| T-type passive | 4 |
| T-type active | 5 |

A **normal tree** is a spanning tree whose total weight is minimum among all spanning trees.

---

## Algorithm

```
PROCEDURE FindAllNormalTrees(Graph G)

    // STEP 1: Assign weights to branches based on element type
    FOR EACH branch b IN G:
        IF type(b) == "A-type active" THEN
            weight(b) = 1
        ELSE IF type(b) == "A-type passive" THEN
            weight(b) = 2
        ELSE IF type(b) == "D-type" THEN
            weight(b) = 3
        ELSE IF type(b) == "T-type passive" THEN
            weight(b) = 4
        ELSE IF type(b) == "T-type active" THEN
            weight(b) = 5
        END IF
    END FOR

    // STEP 2: Find the initial Minimum Spanning Tree (MST)
    // Kruskal's algorithm is recommended to select the first normal tree
    InitialTree = KruskalsAlgorithm(G)
    ListOfNormalTrees = { InitialTree }
    TreesToProcessQueue = { InitialTree }

    // STEP 3: Find all other MSTs via Equivalent Branch Replacement
    WHILE TreesToProcessQueue is NOT empty:
        CurrentTree = DEQUEUE(TreesToProcessQueue)

        // Partition incidence matrix A into [A11 | A12] where A12 represents the twigs of CurrentTree
        A11, A12 = PartitionIncidenceMatrix(G, CurrentTree)

        // Form the fundamental loop matrix (Bf); I is the identity matrix
        Bf = [ I | -Transpose(A11) * Transpose(Inverse(A12)) ]

        // Look at rows of Bf independently to find replacement candidates
        FOR EACH link IN GetLinks(G, CurrentTree):   // Links correspond to rows in Bf

            // The row in Bf defines the unique simple path in the tree for this link
            PathInTree = GetTwigsInFundamentalLoop(Bf, link)

            FOR EACH twig IN PathInTree:
                // If a twig in the path has the exact same weight as the link, they can be swapped
                IF weight(twig) == weight(link) THEN

                    // Create a new minimum spanning tree (normal tree)
                    NewTree = (CurrentTree - { twig }) UNION { link }

                    IF NewTree is NOT IN ListOfNormalTrees THEN
                        ADD NewTree TO ListOfNormalTrees
                        // New trees must also be processed for further branch replacements
                        ENQUEUE NewTree TO TreesToProcessQueue
                    END IF

                END IF
            END FOR
        END FOR
    END WHILE

    // STEP 4: Derive state equations for each tree
    FOR EACH Tree IN ListOfNormalTrees:
        GenerateStructuralEquations(Tree, G)
    END FOR

    RETURN ListOfNormalTrees

END PROCEDURE
```

---

## Equivalent branch replacement

For the current tree, each co-tree **link** defines a fundamental loop with the tree. Along that loop, any tree **twig** with the same weight as the link can be exchanged for the link while keeping total weight unchanged. Repeating this over every discovered tree enumerates all normal trees.

---

## In the app

- **Find All Normal Trees** (Analyze panel) — enumerates normal trees via Kruskal seed and equivalent-branch replacement.
- **Select Manually** — pick tree twigs yourself; the app validates the selection.

State-space derivation for a chosen tree is covered in **Help → Guides → State-Space Derivation**.
