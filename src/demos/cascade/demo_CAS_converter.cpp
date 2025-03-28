// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
//   A small interactive editor to test the
//   convex decomposition settings and the STEP
//   conversion features of OpenCASCADE library
// =============================================================================

#include <cstdlib>

#include "chrono/core/ChRealtimeStep.h"
#include "chrono/core/ChRandom.h"
#include "chrono/collision/ChConvexDecomposition.h"
#include "chrono/physics/ChSystemNSC.h"

#include "chrono_cascade/ChCascadeDoc.h"
#include "chrono_cascade/ChCascadeMeshTools.h"
#include "chrono_cascade/ChCascadeIrrMeshTools.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_HShape.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_StepModelType.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopExp_Explorer.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pnt.hxx>
#include <Prs3d_ShapeTool.hxx>
#include <BRepAdaptor_HSurface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TColgp_HArray1OfVec.hxx>
#include <TColStd_HArray1OfInteger.hxx>
#include <Poly_Connect.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <TColgp_Array1OfDir.hxx>

#include <CSLib_DerivativeStatus.hxx>
#include <CSLib_NormalStatus.hxx>
#include <CSLib.hxx>
#include <TColgp_Array1OfPnt2d.hxx>

#include <TDocStd_Document.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <TDataStd_Name.hxx>
#include <Interface_Static.hxx>
#include <TDF_Label.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_ChildIterator.hxx>
#include <TDF_Tool.hxx>
#include <TObj_TObject.hxx>
#include <TObj_TReference.hxx>
#include <TNaming_NamedShape.hxx>

#include "chrono_irrlicht/ChVisualSystemIrrlicht.h"
#include "chrono_irrlicht/ChIrrMeshTools.h"

// Use the namespace of Chrono
using namespace chrono;
using namespace chrono::irrlicht;

// Use the main namespace of Irrlicht
using namespace irr;

// Use the namespace with OpenCascade stuff
using namespace cascade;

// to make things easier (it's a test..) introduce global variables.

scene::IAnimatedMesh* modelMesh;
scene::IAnimatedMeshSceneNode* modelNode;
scene::ISceneNode* decompositionNode;

ChConvexDecompositionHACDv2 mydecompositionHACDv2;

int hacd_maxhullcount;
int hacd_maxhullmerge;
int hacd_maxhullvertexes;
double hacd_concavity;
double hacd_smallclusterthreshold;
double hacd_fusetolerance;

// LOAD A TRIANGLE MESH USING IRRLICHT IMPORTERS
//
void LoadModel(ChVisualSystemIrrlicht* application, const char* filename) {
    if (modelNode)
        modelNode->remove();
    modelNode = 0;

    if (decompositionNode)
        decompositionNode->remove();
    decompositionNode = 0;

    // Load a mesh using the Irrlicht I/O conversion from mesh file formats.
    modelMesh = application->GetSceneManager()->getMesh(filename);
    modelNode = application->GetSceneManager()->addAnimatedMeshSceneNode(modelMesh);
    modelNode->setMaterialFlag(irr::video::EMF_NORMALIZE_NORMALS, true);
}

// LOAD THE STEP 3D MODEL USING OPEN CASCADE
void LoadStepModel(ChVisualSystemIrrlicht* application, const char* filename) {
    if (modelNode)
        modelNode->remove();
    modelNode = 0;

    if (decompositionNode)
        decompositionNode->remove();
    decompositionNode = 0;

    std::cout << std::endl << std::endl << "0-LOADING THE STEP MODEL..." << std::endl;

    ChCascadeDoc mydoc;
    bool aRes = mydoc.Load_STEP(filename);

    if (aRes) {
        // ---Print hierarchy on screen
        mydoc.Dump(std::cout);

        // ---Find all shapes and get as a single compound
        TopoDS_Shape mshape;
        mydoc.GetRootShape(mshape);

        if (!mshape.IsNull()) {
            // ---Perform Irrlicht triangulation

            scene::SMesh* mmesh = new scene::SMesh();
            video::SColor clr(255, 100, 120, 125);

            ChCascadeIrrMeshTools::fillIrrlichtMeshFromCascade(mmesh, mshape, 0.5);
            // ..also show in Irrlicht view
            scene::SAnimatedMesh* Amesh = new scene::SAnimatedMesh();
            Amesh->addMesh(mmesh);
            mmesh->drop();
            modelNode = application->GetSceneManager()->addAnimatedMeshSceneNode(Amesh, decompositionNode);

            // ---Convert to OBJ Wavefront file

            std::ofstream mobjfile("triangulated_step_model_root.obj");
            // afinder.res_shape.Location(TopLoc_Location()); // to reset CAD reference as center of obj.
            ChCascadeMeshTools::fillObjFileFromCascade(mobjfile, mshape, 0.5);
            std::cout << " ... done!" << std::endl;
        }

    } else
        std::cerr << std::endl << "Error while reading STEP!" << std::endl << std::endl;
}

void DecomposeModel(ChVisualSystemIrrlicht* application) {
    if (decompositionNode)
        decompositionNode->remove();
    decompositionNode = 0;

    if (!modelNode)
        return;

    decompositionNode = application->GetSceneManager()->addEmptySceneNode();

    // Convert the Irrlicht mesh into a Chrono mesh.
    ChTriangleMeshSoup chmesh;
    // modelNode->getMesh();
    fillChTrimeshFromIrlichtMesh(&chmesh, modelNode->getMesh());  // modelMesh->getMesh(0));

    // Perform the convex decomposition using the desired parameters.
    mydecompositionHACDv2.Reset();
    mydecompositionHACDv2.AddTriangleMesh(chmesh);

    mydecompositionHACDv2.SetParameters(hacd_maxhullcount, hacd_maxhullmerge, hacd_maxhullvertexes,
                                        (float)hacd_concavity, (float)hacd_smallclusterthreshold,
                                        (float)hacd_fusetolerance);
    mydecompositionHACDv2.ComputeConvexDecomposition();

    // Visualize the resulting convex decomposition by creating many
    // colored meshes, each per convex hull.
    for (unsigned int j = 0; j < mydecompositionHACDv2.GetHullCount(); j++) {
        scene::SMesh* mmesh = new scene::SMesh();

        // Get the j-th convex hull as a ChTriangleMesh.
        ChTriangleMeshSoup chmesh_hull;
        mydecompositionHACDv2.GetConvexHullResult(j, chmesh_hull);

        video::SColor clr(255, 20 + (int)(140. * ChRandom::Get()), 20 + (int)(140. * ChRandom::Get()),
                          20 + (int)(140. * ChRandom::Get()));

        // Convert the j-th convex hull from a ChTriangleMesh to an Irrlicht mesh.
        fillIrlichtMeshFromChTrimesh(mmesh, &chmesh_hull, clr);

        // Add Irrlicht mesh to the scene, as a scene node.
        scene::SAnimatedMesh* Amesh = new scene::SAnimatedMesh();
        Amesh->addMesh(mmesh);
        mmesh->drop();

        scene::IAnimatedMeshSceneNode* piece_node =
            application->GetSceneManager()->addAnimatedMeshSceneNode(Amesh, decompositionNode);
        piece_node->getMaterial(0).EmissiveColor.set(255, 40, 40, 50);  // 255, 50, 50, 50);
        // piece_node->getMaterial(0).AmbientColor.set(255,30,30,30);//100, 0,0,0);
        piece_node->getMaterial(0).DiffuseColor.set(255, clr.getRed(), clr.getGreen(),
                                                    clr.getBlue());  // 255, 50, 50, 50);
        // piece_node->getMaterial(0).Lighting = true;
        piece_node->setMaterialFlag(irr::video::EMF_NORMALIZE_NORMALS, true);
        scene::IAnimatedMeshSceneNode* piece_node2 =
            application->GetSceneManager()->addAnimatedMeshSceneNode(Amesh, decompositionNode);
        piece_node2->getMaterial(0).Lighting = true;
        piece_node2->getMaterial(0).Wireframe = true;
        piece_node2->getMaterial(0).Thickness = 2;
    }

    modelNode->setVisible(false);
}

void SaveHullsWavefront(ChVisualSystemIrrlicht* application, const char* filename) {
    // Save the convex decomposition to a
    // file using the .obj fileformat.

    try {
        std::ofstream decomposed_objfile(filename);
        mydecompositionHACDv2.WriteConvexHullsAsWavefrontObj(decomposed_objfile);
    } catch (std::exception myex) {
        application->GetGUIEnvironment()->addMessageBox(L"Save file error", L"Impossible to write into file.");
    }
}

void SaveHullsChulls(ChVisualSystemIrrlicht* application, const char* filename) {
    // Save the convex decomposition to a
    // file using the .obj fileformat.

    try {
        std::ofstream decomposed_objfile(filename);
        mydecompositionHACDv2.WriteConvexHullsAsChullsFile(decomposed_objfile);
    } catch (std::exception myex) {
        application->GetGUIEnvironment()->addMessageBox(L"Save file error", L"Impossible to write into file.");
    }
}

// Define a MyEventReceiver class which will be used to manage input
// from the GUI graphical user interface (the interface will
// be created with the basic -yet flexible- platform
// independent toolset of Irrlicht).

class MyEventReceiver : public IEventReceiver {
  public:
    MyEventReceiver(ChVisualSystemIrrlicht* vsys) : vis(vsys) {
        vis->GetDevice()->setEventReceiver(this);

        // create menu
        gui::IGUIContextMenu* menu = vis->GetGUIEnvironment()->addMenu();
        menu->addItem(L"File", -1, true, true);
        menu->addItem(L"View", -1, true, true);

        gui::IGUIContextMenu* submenu;
        submenu = menu->getSubMenu(0);
        submenu->addItem(L"Load OBJ mesh...", 90);
        submenu->addItem(L"Load STEP model...", 95);
        submenu->addItem(L"Save convex hulls (.obj)", 91);
        submenu->addItem(L"Save convex hulls (.chulls)", 96);
        submenu->addSeparator();
        submenu->addItem(L"Quit", 92);
        submenu = menu->getSubMenu(1);
        submenu->addItem(L"View model", 93);
        submenu->addItem(L"View decomposition", 94);

        text_algo_type =
            vis->GetGUIEnvironment()->addStaticText(L"HACDv2 algorithm", core::rect<s32>(510, 35, 650, 50), false);

        // ..add a GUI
        edit_hacd_maxhullcount = vis->GetGUIEnvironment()->addEditBox(
            irr::core::stringw((int)hacd_maxhullcount).c_str(), core::rect<s32>(510, 60, 650, 75), true, 0, 121);
        text_hacd_maxhullcount =
            vis->GetGUIEnvironment()->addStaticText(L"Max. hull count ", core::rect<s32>(650, 60, 750, 75), false);

        // ..add a GUI
        edit_hacd_maxhullmerge = vis->GetGUIEnvironment()->addEditBox(
            irr::core::stringw((int)hacd_maxhullmerge).c_str(), core::rect<s32>(510, 85, 650, 100), true, 0, 122);
        text_hacd_maxhullmerge =
            vis->GetGUIEnvironment()->addStaticText(L"Max. hull merge ", core::rect<s32>(650, 85, 750, 100), false);

        // ..add a GUI
        edit_hacd_maxhullvertexes = vis->GetGUIEnvironment()->addEditBox(
            irr::core::stringw((int)hacd_maxhullvertexes).c_str(), core::rect<s32>(510, 110, 650, 125), true, 0, 123);
        text_hacd_maxhullvertexes = vis->GetGUIEnvironment()->addStaticText(L"Max. vertexes per hull",
                                                                            core::rect<s32>(650, 110, 750, 125), false);

        // ..add a GUI
        edit_hacd_concavity = vis->GetGUIEnvironment()->addEditBox(irr::core::stringw(hacd_concavity).c_str(),
                                                                   core::rect<s32>(510, 135, 650, 150), true, 0, 124);
        text_hacd_concavity = vis->GetGUIEnvironment()->addStaticText(L"Max. concavity (0..1)",
                                                                      core::rect<s32>(650, 135, 750, 150), false);

        // ..add a GUI
        edit_hacd_smallclusterthreshold = vis->GetGUIEnvironment()->addEditBox(
            irr::core::stringw(hacd_smallclusterthreshold).c_str(), core::rect<s32>(510, 160, 650, 175), true, 0, 125);
        text_hacd_smallclusterthreshold = vis->GetGUIEnvironment()->addStaticText(
            L"Small cluster threshold", core::rect<s32>(650, 160, 750, 175), false);

        // ..add a GUI
        edit_hacd_fusetolerance = vis->GetGUIEnvironment()->addEditBox(
            irr::core::stringw(hacd_fusetolerance).c_str(), core::rect<s32>(510, 185, 650, 200), true, 0, 126);
        text_hacd_fusetolerance = vis->GetGUIEnvironment()->addStaticText(L"Vertex fuse tolerance",
                                                                          core::rect<s32>(650, 185, 750, 200), false);

        // .. add buttons..
        button_decompose = vis->GetGUIEnvironment()->addButton(core::rect<s32>(510, 210, 650, 225), 0, 106,
                                                               L"Decompose", L"Perform convex decomposition");

        text_hacd_maxhullcount->setVisible(true);
        edit_hacd_maxhullcount->setVisible(true);
        text_hacd_maxhullmerge->setVisible(true);
        edit_hacd_maxhullmerge->setVisible(true);
        text_hacd_maxhullvertexes->setVisible(true);
        edit_hacd_maxhullvertexes->setVisible(true);
        text_hacd_concavity->setVisible(true);
        edit_hacd_concavity->setVisible(true);
        text_hacd_smallclusterthreshold->setVisible(true);
        edit_hacd_smallclusterthreshold->setVisible(true);
        text_hacd_fusetolerance->setVisible(true);
        edit_hacd_fusetolerance->setVisible(true);
    }

    bool OnEvent(const SEvent& event) {
        // check if user moved the sliders with mouse..
        if (event.EventType == EET_GUI_EVENT) {
            s32 id = event.GUIEvent.Caller->getID();
            auto env = vis->GetGUIEnvironment();

            switch (event.GUIEvent.EventType) {
                case gui::EGET_MENU_ITEM_SELECTED: {
                    // a menu item was clicked

                    gui::IGUIContextMenu* menu = (gui::IGUIContextMenu*)event.GUIEvent.Caller;
                    id = menu->getItemCommandId(menu->getSelectedItem());

                    switch (id) {
                        case 90:
                            env->addFileOpenDialog(L"Load a mesh file, in .OBJ/.X/.MAX format", true, 0, 80);
                            break;
                        case 95:
                            env->addFileOpenDialog(L"Load a 3D CAD model, in STEP format", true, 0, 85);
                            break;
                        case 91:
                            env->addFileOpenDialog(L"Save decomposed convex hulls in .obj 3D format", true, 0, 81);
                            break;
                        case 96:
                            env->addFileOpenDialog(L"Save decomposed convex hulls in .chulls 3D format", true, 0, 86);
                            break;
                        case 92:  // File -> Quit
                            vis->GetDevice()->closeDevice();
                            break;
                        case 93:
                            if (modelNode)
                                modelNode->setVisible(true);
                            if (decompositionNode)
                                decompositionNode->setVisible(false);
                            break;
                        case 94:
                            if (modelNode)
                                modelNode->setVisible(false);
                            if (decompositionNode)
                                decompositionNode->setVisible(true);
                            break;
                    }
                    break;
                }

                case gui::EGET_FILE_SELECTED: {
                    // load the model file, selected in the file open dialog
                    gui::IGUIFileOpenDialog* dialog = (gui::IGUIFileOpenDialog*)event.GUIEvent.Caller;

                    switch (id) {
                        case 80:
                            LoadModel(vis, core::stringc(dialog->getFileName()).c_str());
                            break;
                        case 85:
                            LoadStepModel(vis, core::stringc(dialog->getFileName()).c_str());
                            break;
                        case 81:
                            SaveHullsWavefront(vis, core::stringc(dialog->getFileName()).c_str());
                            break;
                        case 86:
                            SaveHullsChulls(vis, core::stringc(dialog->getFileName()).c_str());
                            break;
                    }
                } break;

                case gui::EGET_EDITBOX_ENTER: {
                    // load the model file, selected in the file open dialog
                    gui::IGUIEditBox* medit = (gui::IGUIEditBox*)event.GUIEvent.Caller;

                    switch (id) {
                        case 122:
                            hacd_maxhullmerge = std::atoi(irr::core::stringc(medit->getText()).c_str());
                            medit->setText(irr::core::stringw(hacd_maxhullmerge).c_str());
                            break;
                        case 123:
                            hacd_maxhullvertexes = std::atoi(irr::core::stringc(medit->getText()).c_str());
                            medit->setText(irr::core::stringw(hacd_maxhullvertexes).c_str());
                            break;
                        case 124:
                            hacd_concavity = std::atof(irr::core::stringc(medit->getText()).c_str());
                            medit->setText(irr::core::stringw(hacd_concavity).c_str());
                            break;
                        case 125:
                            hacd_smallclusterthreshold = std::atof(irr::core::stringc(medit->getText()).c_str());
                            medit->setText(irr::core::stringw(hacd_smallclusterthreshold).c_str());
                            break;
                        case 126:
                            hacd_fusetolerance = std::atof(irr::core::stringc(medit->getText()).c_str());
                            medit->setText(irr::core::stringw(hacd_fusetolerance).c_str());
                            break;
                    }
                } break;

                case gui::EGET_BUTTON_CLICKED: {
                    switch (id) {
                        case 106:
                            DecomposeModel(vis);
                            return true;
                        default:
                            return false;
                    }
                    break;
                }

                default:
                    break;
            }
        }

        return false;
    }

  private:
    ChVisualSystemIrrlicht* vis;

    gui::IGUIContextMenu* menu;
    gui::IGUIButton* button_decompose;
    gui::IGUIStaticText* text_algo_type;
    gui::IGUIStaticText* text_hacd_maxhullcount;
    gui::IGUIEditBox* edit_hacd_maxhullcount;
    gui::IGUIStaticText* text_hacd_maxhullmerge;
    gui::IGUIEditBox* edit_hacd_maxhullmerge;
    gui::IGUIStaticText* text_hacd_maxhullvertexes;
    gui::IGUIEditBox* edit_hacd_maxhullvertexes;
    gui::IGUIStaticText* text_hacd_concavity;
    gui::IGUIEditBox* edit_hacd_concavity;
    gui::IGUIStaticText* text_hacd_smallclusterthreshold;
    gui::IGUIEditBox* edit_hacd_smallclusterthreshold;
    gui::IGUIStaticText* text_hacd_fusetolerance;
    gui::IGUIEditBox* edit_hacd_fusetolerance;
};

//
// This is the program which is executed
//

int main(int argc, char* argv[]) {
    // 1- Create a Chrono physical system: all bodies and constraints
    //    will be handled by this ChSystemNSC object.
    ChSystemNSC sys;

    // 4- Create the Irrlicht visualization system
    auto vis = chrono_types::make_shared<ChVisualSystemIrrlicht>();
    vis->AttachSystem(&sys);
    vis->SetWindowSize(800, 600);
    vis->SetWindowTitle("Convex decomposition of a mesh");
    vis->Initialize();
    vis->AddLogo();
    vis->AddSkyBox();
    vis->AddCamera(ChVector3d(0, 1.5, -2));
    vis->AddTypicalLights();

    // Initial settings
    modelMesh = 0;
    modelNode = 0;
    decompositionNode = 0;

    hacd_maxhullcount = 512;
    hacd_maxhullmerge = 256;
    hacd_maxhullvertexes = 64;
    hacd_concavity = 0.2;
    hacd_smallclusterthreshold = 0.0;
    hacd_fusetolerance = 1e-9;

    //
    // USER INTERFACE
    //

    // Create some graphical-user-interface (GUI) items to show on the screen.
    // This requires an event receiver object.
    MyEventReceiver receiver(vis.get());

    // note how to add a custom event receiver to the default interface:
    vis->AddUserEventReceiver(&receiver);

    // Rendering loop
    while (vis->Run()) {
        vis->BeginScene();
        vis->Render();
        vis->EndScene();
    }

    return 0;
}
