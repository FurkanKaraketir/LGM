## IMECE2003-41237

## SYSTEMATIC STATE EQUATION DERIVATION FOR LINEAR SYSTEMS USING THE NORMAL TREE METHOD

Kerem Altun

Middle East Technical University

Then the proposed algorithm to determine all normal trees of a system graph is stated, followed by a systematic state equation derivation procedure. The paper concludes with two well- known statements which surprisingly do not appear in textbooks on system dynamics and graph theory.

A systematic method to derive the state equations of a linear system starting from its linear graph is proposed. The normal tree is used in the analysis, which is a method to determine the dependencies between energy storage elements in the system. An algorithm to list all normal trees of a system graph is developed, which enables the determination of energy-based state variable sets and corresponding state equations. A computer program is developed to realize these algorithms, which derives the state equations of a system, given its linear graph.

To get the reader familiar with linear graph modeling, a brief explanation about the concept will be given in this section.

An oriented linear graph is a discrete structure composed

of a set of objects objects

,Κ} called branches, where each branch is

The concept of modeling of linear systems is by no means

associated with an ordered pair of nodes ni n, j called the

obsolete, nor trivial; since many phenomena in nonlinear systems can be investigated using the clues obtained from linear system behavior. This concept has been worked on extensively especially in the third quarter of the past century, and many methods have been developed both for modeling and for analysis of linear systems. Among those modeling methods, graphical methods have been introduced. The most common of such methods are the linear graph method and the bond graph method. It is not our concern here to discuss the comparative superiorities of these methods, however, it is worth stating that both methods are very similar, and both use a port-based approach to dynamical system behavior.

be.

end nodes of that branch

Bülent E. Platin

Tuna Balkan

Middle East Technical University

Middle East Technical University

## ABSTRACT

## Linear Graph Representation of Dynamical Systems

,Κ} called nodes, and another set of

## INTRODUCTION

The purpose of this study is to determine a systematic method to derive the state equations of a system starting from its linear graph, and to automate this equation derivation by means of a user-friendly software program. In the analysis, the normal tree method is used, the details of which are given in the paper. The systems in the scope of this study are limited to linear, time-invariant, lumped parameter systems composed of one-port and two-port elements only.

*Figure 1 – An example of an oriented*

*linear graph.*

In this paper, the concept of linear graph is reviewed and the unfamiliar reader is directed to a number of references.

To better visualize the incidence relations, oriented graphs are generally represented by a diagram, where nodes are


represented as points and branches as directed line segments between their end nodes, consistent with the ordered pairing of nodes with which they are associated. An example of an oriented linear graph is given in Figure 1.

This paper is not concerned with the mathematical approach to graphs, so related mathematical definitions and/or explanations are not to be given here. Matrix representation of graphs is not to be discussed either. [1] summarizes the graph theoretic concepts and matrix representations related with this study. [2] is a good reference for further exploration on the subject and can be consulted for a detailed explanation.

In a graph representing a dynamical system, each branch corresponds to an energy port of a lumped element. Associated with each port, i.e., each branch, two variables are defined: an across variable and a through variable. These are physically motivated system variables; across variables are measured across an element and imply a difference with respect to a reference value, through variables are measured through an element. The across variable is the velocity (with respect to an inertial reference frame) for mechanical elements, pressure (with respect to a reference pressure, e.g., atmospheric pressure) for hydraulic elements, etc. The through variable is force for a mechanical element, current for an electrical element, etc.

In the linear graph of a system, each node corresponds to a distinct across variable location. The directions on the branches indicate the positive directions for the across and through variables for that energy port. The linear graph looks very similar to an electrical circuit; for electrical systems, the linear graph and the circuit diagram look more or less the same. The linear graph of a system can be constructed in two steps: First the distinct across variable locations on the system should be identified and represented as nodes on the graph, then the lumped elements in the system should be identified and represented as branches in the graph, consistent with the across variables at their terminals in the corresponding system. A sample system and its linear graph are given in Figure 2.

This is an electrical system with two capacitances, two inductances, two resistances, one voltage source and one current source. This system consists of one-port elements only, i.e., in all the components of the system the energy exchange with the surroundings take place through a single energy port. In other words, the components of the system either store or dissipate energy, but do not transform it. This study involves two-port elements as well, which model the transformation of energy between two energy domains. An example involving two-port elements is given in the later sections of this paper.

Details on the concept of energy-based modeling will not be given here; the reader can consult [3] for a more thorough explanation.

## The Normal Tree

Before discussing the normal tree of a linear graph, let us give a definition of a spanning tree (or a tree) of a graph. A tree of a graph is defined as a special subgraph that is connected, does not have any loops, and includes all the nodes of the graph. Branches included in a tree are called twigs; branches not included in a tree are called links. As it follows from the definition, there are many trees for a given linear graph.

*Figure 2 – An electrical system and*

*its linear graph.*

Finding state equation representations of dynamic systems using linear graphs involves the simultaneous manipulation of two sets of equations: elemental equations and structural equations. Elemental equations are the equations representing the dynamical behaviors of individual elements of the system. They are first order differential equations for energy storage elements and algebraic equations for dissipative elements and transducers, written in terms of through and across variables associated with the system. Structural equations are algebraic equations and they represent the interconnection of individual elements in the system. They are classified into two groups: continuity equations and compatibility equations. Continuity equations are written in terms of currents, forces, etc. (i.e., through variables) using fundamental cutsets and state that the algebraic sum of the through variables of the branches in any cutset vanishes. Compatibility equations are written in terms of voltages, velocities, etc. (i.e., across variables) using fundamental loops and state that the algebraic sum of the across variables of the branches around any loop vanishes. Fundamental loops and fundamental cutsets are found with respect to an arbitrarily chosen tree of the graph, the details of which will not be discussed here. [4] is a good reference to consult on that subject. The continuity and compatibility equations are first introduced by Kirchoff together with the definition of a tree and his famous theorem concerning trees [1]. Thus, for electrical systems, the continuity and compatibility equations are known as Kirchoff’s current equations and Kirchoff’s voltage equations, respectively.


After defining the tree and fundamental loops and cutsets accordingly, the fundamental loop and fundamental cutset matrices are used as coefficient matrices for the continuity equations. The details on how to construct these matrices can also be found in [4].

In dynamic system analysis, an appropriate energy-based state variable set would be the across variables of A-type passive elements (masses, capacitors, etc.), and the through variables of T-type passive elements (springs, inductors, etc.) since the derivatives of these variables already appear in elemental equations, and they are the variables in terms of which the energy is stored. If such a state variable set is to be selected, then for minimum equation manipulation, a normal tree of the system should be selected when writing the structural equations. Apart from the advantage of minimum equation manipulation, the normal tree defines a set of independent energy storage elements for the system, which is explained in detail below.

A normal tree is a special tree having the following properties based on the energetic behavior of individual elements forming the corresponding system:

- All A-type active elements are in the tree.

- Maximum possible number of A-type passive elements in the tree.

- Minimum possible number of T-type passive elements in the tree.

- No T-type active elements are in the tree.

In addition to these properties, if the system has transformers or gyrators (i.e., two-port elements), there are two additional constraints on the normal tree:

- Only one branch of each transformer can be included in a normal tree.

- Either both or none of the branches of a gyrator can be included in a normal tree.

These constraints appear because of causality, which is explained in detail in [3].

Those A-type passive elements that are in the tree plus those T-type passive elements that are not in the normal tree are declared as independent energy storage elements. The total number of such elements dictates the dynamic complexity of the system, which is commonly referred to as its order. This means that, only those A-types that are in the tree and those T-types that are not in the tree are independent, so their corresponding variables can be selected as state variables. The other energy storage elements in the system are called as dependent energy storage elements.

As an example, Figure 3 shows the previous example with one of its normal trees selected.

As seen in Figure 3, the tree contains the only A-type active element, one A-type passive element (capacitor), one D- type passive element (resistor) and one T-type passive element. One A-type passive element must be left out of the tree, and one T-type passive element must be included. A careful examination of the graph reveals that there is no way to include both A-type passives, or exclude both T-type passives when forming the normal tree. Thus, there are two independent energy storage elements in this system, and, according to this choice of the normal tree, they are C5 and L7. The other capacitance C4 and the inductance L3 are dependent.

If there are dependent energy storage elements in a system, the normal tree of the system graph is not unique. That

*Figure 3 – A system graph and one of its normal trees.*

is, the dependence or independence of the system elements depends on the choice of the normal tree. One of the main purposes of this study is to find a systematic method to find all normal trees of a given general system graph. One should here note that finding all normal trees of the system graph has no practical significance, as far as the solution of the system is concerned. That is, a normal tree has no superiority over the others. However, in a system involving dependent energy storage elements, if some energy storage elements are of more significance than the others and their corresponding variables are preferred to be declared as state variables, then the corresponding normal tree can be chosen among all the normal trees as to end up in a solution with minimum equation manipulation.

## NORMAL TREE ALGORITHM

The main contribution of this work is the systematic determination of all normal trees of a system graph, which leads to all energy-based state variable representations of the corresponding dynamical system. An improved version of the method given in [5] is used for this purpose.

If the priorities among branches are considered, when forming a normal tree, all A-type active branches must be included, then as much as possible A-type passive elements should be included, then D-type elements, T-type passives and T-type actives, respectively.

This priority sequence leads to forming a weighted graph. Some positive integer numbers called weights are assigned to the branches of the graph such that:

wAA : wAP :

weight assigned to A-type active elements

weight assigned to A-type passive elements

wDP:

weight assigned to D-type elements

wTP :

weight assigned to T-type passive elements

wTA :

weight assigned to T-type active elements

A careful examination of the weights in graph trees shows that normal trees of a graph have the minimum total weight among all other trees. Then the problem is converted to a minimum weight spanning tree (MST) problem, which is well known in the area of combinatorial mathematics and other branches of engineering.


simple path joining the end nodes of branches 5 and 6. Branch 6 can not replace any other branch since its weight is larger than all the branches in the path joining its nodes, so it can not exist in any MST. Similarly, branch 8 is not in any MST, since there are no branches of weight 5 in the path in the tree joining its end nodes. However, branch 5 can be replaced instead of branch 4 to form another MST, as well as branch 7 can be replaced instead of branch 3 to form another MST. So the number of minimum spanning trees for this graph is 4. All MSTs of the graph are shown in Figure 5.

Finding one MST of a given weighted graph is one of the classical problems related with computer algorithms, and there are many available algorithms for this purpose. An extensive survey on that subject can be found in [6]. However, the problem of listing all MSTs of a weighted graph has not been attacked that extensively, and the studies on that subject are not more than a few. This study is one of the few solution attempts to this problem. Furthermore, the application of the problem of finding all MSTs of a graph to dynamic system analysis (i.e., the problem of finding all normal trees) has not been reported before, to the authors’ knowledge.

Let us first start with assigning weights to the branches. The weights that are proposed to assign are simply wAA = 1, wAP = 2, wDP = 3, wTP = 4, wTA = 5. This particular selection of weight set is not strict, any other combination of weights that satisfies the inequality (1) could also be assigned. Then, one MST is selected using one of the available algorithms such as that of Kruskal. An explanation of the algorithm appears in [2]. It can be stated as,

“List all branches of the graph in the order of non- decreasing weight. Next, select the smallest weight (i.e., the first) branch on the list. Then for each successive step select (from all remaining branches of the graph) another smallest weight branch that makes no loop with the previously selected branches. Continue until (N – 1) branches have been selected.”

This is a simple statement and it directly follows from the definitions of a tree and a minimum spanning tree. The rest of the algorithm will be explained on the example graph, whose equivalent weighted graph and one of its MSTs are shown in Figure 4. The numbers written on a branch signify its index and weight, respectively.

*Figure 5 – All MST’s of the example graph.*

This algorithm gives a general procedure applicable to find all MSTs of any weighted graph, which, in this case, corresponds to finding all normal trees of a system graph.

However, the algorithm is not completely stated. For extreme cases with high levels of dependencies between branches, (i.e., for testing purposes, when finding all MST’s of a graph whose branch weights are all equal) the algorithm as stated here does not work. It tends to generate branch lists that do not correspond to an MST. The following theorem resolves this problem:

“The algorithm generates a branch list that does not correspond to a minimum spanning tree if and only if it generates that branch list exactly twice.”

In extreme cases, the algorithm generates branch lists that do not correspond to an MST; however, it generates that branch list exactly twice. So after execution of the algorithm, one should check for duplicate branch lists and remove them to end up in a complete list of MSTs for a given weighted graph. The proof of the theorem is not to be stated here, but a proof and an example graph where this problem is observed appears in [7].

*Figure 4 – A weighted graph and one*

*of its MST’s.*

Next, the links of the tree are considered. For each link in the co-tree, there is a unique simple path in the selected tree that joins the end nodes of that link . If, in that path, there exists a branch (twig) b j with the same weight as the link , then can be replaced with to form another minimum spanning tree [5]. This action is called an equal branch replacement. By performing successive equal branch replacements, one can construct all MSTs of a given weighted graph. In the example, branches 1 and 4 form the unique

## Extension to Graphs with Two-ports

The algorithm for two-ports is the same; once the weighted graph is formed, the algorithm presented in the previous section is executed to determine all the normal trees of the system. However, the main problem is to determine the weights for the two-port branches.

For the case with one-port elements, the relative priorities among branches (in terms of whether or not to be included in a


normal tree) are fixed according to the element types, and do not depend on the structure of the graph. However, for two- port branches, this is not the case. Weights of two-port branches are not fixed values, just as their priorities to be included in a normal tree; it totally depends on the structure of the graph. A procedure to assign weights is proposed in this study, based on the facts that

- In its own medium, a single two-port branch is the only representative of the medium to which its pair is connected.

- The transformers reflect the media on one side to the other side by a constant factor only, in contrast to gyrators, which reflect the media on one side as its dual to the other side.

Based on these facts, the following procedure is proposed: First all the two-port branches are removed from the graph. In this study, graphs containing two-port elements are treated as disconnected graphs whose connected components correspond to different physical media in the system. Removing the two- port branches ends up in a disconnected graph whose branch weights are all known, and assigned consistently with the previous section. Then one minimum spanning forest† is selected according to the algorithm of Kruskal, treating connected components separately.

Then, the paths in the tree for the two-port branches are considered, and the twig in the path with the highest weight is determined for each branch. Then the weight assignment is done as follows:

For a transformer branch, assign its weight as the weight of the highest weighted branch in the path joining the end nodes of its pair.

For a gyrator branch, assign its weight as the complementary weight of the highest weighted branch in the path joining the end nodes of its pair.

The complementary weight of the weight of a branch is defined as, using the notation in the previous section:

If the weights are assigned as wAA = 1, wAP = 2, wDP = 3, wTP = 4, wTA = 5, then the complementary of 1 is 5, the complementary of 2 is 4, the complementary of 3 is 3, the complementary of 4 is 2 and the complementary of 5 is 1.

However, the weight assignment procedure given above works only for cases where there are no structural dependencies between the two-port branches. If the two-port elements in the system are dependent in the structural equations, it may happen that the assignment of the weight of a two-port branch may change the graph such that the initially selected MST is not an MST anymore. Thus, the path joining end nodes of another two-port branch may have changed accordingly as well. This problem may be overcome by simply postponing the weight assignment of a two-port branch if the path joining the end nodes of its pair has the

*Figure 6 – Two-port weight assignment*

*algorithm*

“possibility” to be modified according to the forthcoming weight assignments. The weight assignment is postponed until the “possibility” does not exist anymore, i.e., the path joining the end nodes of the pair of the branch is determined exactly


After removing the two-ports from the graph, an MST is found. Then the algorithm scans through the two-port branches and for each one, it considers the path in the tree that joins the end nodes of that two-port branch. For branch 3, the path joining its end nodes contains a single branch of weight 5, so the pair of branch 3 (namely branch 4) should have a weight of 5. With this weight assigned, the program finds another MST and continues with that MST. For branch 4, the path joining its end nodes has the “possibility” to be modified since there exists a still unweighted two-port branch. So the algorithm skips that branch and continues with branch 6. For branch 6, the path joining its end nodes contains a single branch of weight 2. So the pair of branch 6 (branch 7) should be assigned a weight equal to the complementary of 2 (since this is a gyrator pair), which is 4. Then a possibly new MST is found since the graph is modified. For branch 7, the path joining its end nodes is fixed and can not change, and the highest weighted branch in the path has a weight of 3. So the pair of branch 7 (namely branch 6) has to be assigned a weight equal to the complementary of 3 (since this is a gyrator pair), which is again 3. Another MST is found since the graph is modified. There are still unweighted branches left, so the algorithm begins scanning once more. The weight of the pair of branch 3 is already assigned so it is skipped. For branch 4, now the “possibility” of modification of its path does not exist any more since branch 6 is already assigned a weight. The highest weighted branch in the path of branch 4 has a weight

with other weight assignments. This act of postponing is in fact subconsciously done when finding the normal tree of a complicated system graph manually. Furthermore, since at first all two-port branches are removed, there may not exist a path joining the end nodes of the pair of a two-port branch, thus its weight can not be assigned.

An algorithm to overcome these problems is presented as a flowchart in Figure 6. This algorithm postpones the weight assignment of a two-port branch if at least one element in the path joining the end nodes of its pair is also in the path joining the end nodes of another two-port branch.

*Figure 7 – A hydromechanical system and its*

*linear graph.*

The execution of the two-port weight assignment algorithm can be shown on the example hydromechanical system given in Figure 7. The equivalent weighted graph of this system is given in Figure 8. The numbers written on each branch signify its index and weight respectively. The weights of one-ports are assigned consistently with the procedure given in the previous section. The weight assigned for the two- port branches is 6, which corresponds to removing all the two- port branches from the graph.

*Figure 9 – The normal tree of the system graph*

*of Figure 7.*

of 2, so the pair of branch 4 (namely branch 3) has to be assigned a weight of 2. There are no unweighted branches left, so the algorithm stops. With these weights assigned, the normal tree finding procedure is called, which ends up in a single normal tree as shown in Figure 9. However, the normal tree finding procedure should be modified slightly to be consistent with causality constraints, i.e., to exclude branch lists that involve both or none of the transformer branch pairs, and only one branch from each gyrator.

After finding the normal trees of the system graph, the state equations of the system are to be found. This study proposes a systematic method to derive the state equations from a linear graph and a selected normal tree (i.e., a selected energy-based state variable set).

*Figure 8 – A weighted graph involving two-port*

*elements.*


## DERIVATION OF STATE EQUATIONS

Once the system graph is formed and a normal tree is selected, derivation of the state equations is straightforward. [8] gives the state equation derivation procedure for systems with one-port elements only, where dependent energy storage elements may exist. It gives recommendations for the extension to transformer two-port elements as well. The extension to systems with two-ports has been done in both [9] and [10]. However, methods given in both of those studies, as well as the recommendations of Kuh & Rohrer, are found to be incomplete with some incorrect assumptions, and, to the authors’ knowledge, there does not exist any previous work that systematically derives the state equations using the linear graph for systems in the scope of this study. However, all three methods are very similar, and the addition of a pre- processing algorithm enables the usage of these methods.

First, the method which is a slight modification of [8] will be summarized for one-port elements only. It can be shown that the structural equations can be written in matrix form as:

where the F matrices are defined consistently with [8]. Here the subscript S refers to A-type passive elements not in the normal tree, C refers to A-type passive elements in the normal tree, R refers to D-type passive elements not in the normal tree, G refers to D-type passive elements in the normal tree, L refers to T-type passive elements not in the normal tree, refers to T-type passive elements in the normal tree, V refers to A-type active elements (that are definitely in the normal tree) and I refers to T-type active elements (that are definitely not in the normal tree).

These structural equations express secondary variables explicitly in terms of primary variables. Primary variables are defined as the across variables of those branches that are in the tree plus the through variables of those branches that are not in the tree. Secondary variables are defined as their complementaries, i.e., across variables of the branches that are not in the tree plus through variables of branches that are in the tree. Note that the selection of a normal tree provides that maximum number of the corresponding variables of A-type or T-type energy storage elements (i.e., the state variables) appear as primary variables.

Let us denote the state variables by x and their complementary by z, the primary variables of the resistive elements by d and their complementary by e, the primary variables of the dependent energy storage elements by q and their complementary by r, and the input variables by s. Then the structural equations take the form:

Note that the secondary variables of the dependent energy storage elements are expressed only in terms of the state

variables and/or inputs. Apart from the structural equations, elemental equations are needed, which characterize the behavior of individual lumped elements of the system. They can be written in matrix form as:

where Kx, Kd, and Kq are diagonal matrices containing the system parameters. General practice is to eliminate the secondary variables by substituting the structural equations into elemental equations. This results in the following set of equations:

Solving for d in equation (4), and substituting d and

equation (5) to equation (3), x is obtained in terms of x and s, which is the desired state variable formulation in the so-called normal form [8]:

The detailed derivation procedure to obtain the state equations in normal form is given in [9] and will not be recited here. The result is

where

## Extension to Systems with Two-ports

As stated before, for systems with two-port elements, the formulation should be pre-processed. That is because the two- port variables will appear in the equation set (2) in both the primary variable vector and the secondary variable vector, and the derivation becomes difficult using this method as it is.

Denoting the two-port branches in the selected tree by and those not in the tree by the structural equations can be written in matrix form as [7]:


## where

Elemental equations for the two-ports can be written as:

where g denotes the two-port primary variables and h denotes the two-port secondary variables. Here note that K g is not a diagonal matrix as the other coefficient matrices in the elemental equations.

Applying the procedure for one-ports as it is yields a set

of equations like:

where 1f and are linear functions with respect to their arguments. Further elimination of g is necessary to obtain state

equations in the form x

Ax+Bs+Cs

, which is not possible

without integration in general. In fact, equations (6) are not that coupled and may be solved by hand. However, a systematic procedure is not applicable. Instead, we propose to reduce the set of equations to equation (2). To do this, different from the standard practice of substituting the structural equations into elemental equations to get rid of secondary variables, we use two-port elemental equations to eliminate all the two-port variables.

The procedure is as follows: each individual structural equation involving a single secondary variable of a one-port is traced to determine whether a primary two-port variable exists. If it does not depend on a two-port variable, it can be represented as in equation (2) and it creates no problem. Else, if it does depend on a primary two-port variable, that two-port variable is replaced using the elemental equation with its corresponding secondary variable, and then this secondary variable is replaced using its structural equation. If the two- port branches are not structurally dependent in the system

graph, this yields a single structural equation with one-port variables only. If there is structural dependence between the two-ports, this procedure is applied sequentially until no two- port variable exists. This procedure, physically speaking, is similar to creating an equivalent graph without two-ports where the element parameters and/or types are modified according to transformer or gyrator ratios.

Thus the structural equations are reduced to the form in equation (2), and the procedure for one-ports is applicable. Note that the zero blocks in the coefficient matrix in equation (2) still appear, however, the zero blocks inside the Z, E and R matrices may not be present. That is because now a through variable can be expressed in terms of an across variable if there are gyrator branches in the system. Furthermore, the coefficient matrix is not composed only of 1’s and 0’s and – 1’s anymore. But the procedure is still applicable.

## THE COMPUTER PROGRAM

This study also involves the development of a software package program with a graphical user interface. This program, to the authors’ knowledge, is among the very few that accept the system linear graph as the input. It is aimed to be used mainly for educational purpose, but it can be used in any application where a linear graph modeling is appropriate.

The program consists of mainly three modules: the user interface, normal tree determination algorithm, and state equation generation. The normal tree determination algorithm and the state equation derivation procedure are exactly the same described in the previous sections.

The program is under constant construction. The current features of the program can be stated as follows:

- User-friendly interface, allowing drag & drop operations to enter the linear graph input to the system.

- Possibly the first linear graph modeling software to operate in Windows® environment.

- Unique feature of listing all the normal trees, enabling to find all possible energy-based state variable representations.

- General-purpose minimum spanning tree algorithm. The algorithm can be used to find all MSTs of any graph.

## SUMMARY AND CONCLUSIONS

In this study, a systematic procedure to find all normal trees of a system graph is proposed. The idea of forming an equivalent weighted graph is inspired from [11]. Having formed the weighted graph, the procedure for one-ports is mostly based on [5], but the extension to two-port elements is original. The state equation derivation procedure is mostly based on [8]. A similar method also appears in [12], however, [12] states that there is no systematic procedure available for two-port elements. This study proposes a solution to this problem. However, the two-port variables are immediately eliminated, which will cause a problem in formulating the output equations, especially if one of the output variables is a two-port variable. The systematic formulation of output equations is not considered in this study.

It is believed that further work on this study can be done on the systematic formulation of output equations, the


extension of the method to nonlinear systems, or the extension to multiport elements.

The software developed in this study is not a professional and commercial one, and is not comparable to already existing modeling and simulation packages like SIMULINK®, Dymola®, 20-sim®, EASY5®, ITI-SIM® etc. in any aspect. However, it is among the very few packages that accept the linear graph as the input, and, to the authors’ knowledge, the first and only to operate in Windows® environment.

Two well-known very obvious statements arose in the

study, based on the observations of [13], which surprisingly have not made their way into the textbooks on system dynamics as explicit statements:

- Any two minimum spanning trees in a given graph have the same branch weight spectrum [13]. Translated into system dynamics domain, this means that in addition to the constancy of the order of the system according to any normal tree, the number of independent A-type passive elements and independent T-type passive elements are constant for a given dynamic system.

- Every branch is replaced with another branch of equal weight to form another minimum spanning tree, which implies that a dependent A-type passive element is energetically dependent on other A-type passive elements and a T-type passive element is dependent on other T-type passive elements. Cross- dependency is not possible.

These two statements hold for systems without gyrators. For systems with gyrators, the opposite of the second statement is correct: only cross-dependencies are possible between the energy domains connected with a gyrator.

## REFERENCES

- 1. Altun, K., Balkan, T., Platin, B. E., (2002), "Extraction of State Variable Representations of Dynamic Systems Employing Linear Graph Theory", Proceedings of the Sixth International Conference on Mechatronic Design and Modeling (MDM2002), 4-6 September 2002, Cappadocia, Turkey, pp. 17-29.

- 2. Deo, N., (1974), Graph Theory with Applications to Engineering and Computer Science, Prentice Hall, Inc.

- 3. Rowell, D., Wormley, D., (1997), System Dynamics: An Introduction, First Edition, Prentice Hall, Inc.

- 4. Seshu, S., Reed, M. B., (1961), Linear Graphs and Electrical Networks, Addison-Wesley.

- 5. Wright, P., (1997), “Counting and Constructing Minimal Spanning Trees”, Bulletin of the Institute of Combinatorics and Its Applications, 21, pp. 65-76.

- 6. Graham, R. L., Hell, P., (1985), “On the History of the Minimum Spanning Tree Problem”, Annals of the History of Computing, 1, pp. 43-57.

- 7. Altun, K., (2002), “Computer Aided Analysis of Dynamic Systems Employing Graph Theory”, M.S. Thesis, METU Mechanical Engineering Department, Ankara

- 8. Kuh, E. S., Rohrer, R. A., (1965), “The State -Variable Approach to Network Analysis”, Proc. IEEE, 53, pp. 672-686.

- 9. Gürsöz, E. L., (1981), “Computerized Formulation and Analysis of Dynamic Systems via Graph Theory”, M. S. Thesis, METU Mechanical Engineering Department, Ankara.

- 10. Filho, A. A., Gonçalves, J. B., (1999), “Automatic Modeling of Lumped Parameter Dynamic Systems”, SBA Controle & Automação, Vol. 10, No. 01.

- 11. Díaz-Calderon, A., Paredis, C. J. J., Khosla, P. K., (1999), “On the Synthesis of System-Level Dynamic Equations for Mechatronic Systems”, Tech. Report 04 - 06-99, Institute for Complex Engineered Systems, Carnegie-Mellon University, Pittsburgh, PA.

- 12. Koenig, E. H., Tokad, Y., Kesavan, H. K. (1967), Analysis of Discrete Physical Systems, McGraw-Hill.

- 13. Wright, P., (2000), “On Minimum Spanning Trees and Determinants”, The Mathematical Association of America, Mathematics Magazine, February 2000.

- 14. Shearer, J. L., Murphy, A. T., Richardson, H. H., (1967), Introduction to System Dynamics, Addison- Wesley.
