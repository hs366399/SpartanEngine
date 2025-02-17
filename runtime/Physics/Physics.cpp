/*
Copyright(c) 2016-2022 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ===================================================================
#include "pch.h"
#include "Physics.h"
#include "PhysicsDebugDraw.h"
#include "BulletPhysicsHelper.h"
#include "../Profiling/Profiler.h"
#include "../Rendering/Renderer.h"
#include "../Input/Input.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Camera.h"
SP_WARNINGS_OFF
#include "BulletCollision/BroadphaseCollision/btDbvtBroadphase.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletSoftBody/btSoftBodyRigidBodyCollisionConfiguration.h"
#include "BulletCollision/CollisionDispatch/btCollisionDispatcher.h"
#include "BulletCollision/NarrowPhaseCollision/btRaycastCallback.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"
#include "BulletSoftBody/btSoftRigidDynamicsWorld.h"
#include "BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h"
SP_WARNINGS_ON
//==============================================================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    static const bool m_soft_body_support = true;

    Physics::Physics(Context* context) : ISystem(context)
    {
        m_broadphase        = new btDbvtBroadphase();
        m_constraint_solver = new btSequentialImpulseConstraintSolver();

        if (m_soft_body_support)
        {
            // Create
            m_collision_configuration = new btSoftBodyRigidBodyCollisionConfiguration();
            m_collision_dispatcher    = new btCollisionDispatcher(m_collision_configuration);
            m_world                   = new btSoftRigidDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);

            // Setup         
            m_world_info = new btSoftBodyWorldInfo();
            m_world_info->m_sparsesdf.Initialize();
            m_world->getDispatchInfo().m_enableSPU = true;
            m_world_info->m_dispatcher             = m_collision_dispatcher;
            m_world_info->m_broadphase             = m_broadphase;
            m_world_info->air_density              = (btScalar)1.2;
            m_world_info->water_density            = 0;
            m_world_info->water_offset             = 0;
            m_world_info->water_normal             = btVector3(0, 0, 0);
            m_world_info->m_gravity                = ToBtVector3(m_gravity);

        }
        else
        {
            // Create
            m_collision_configuration = new btDefaultCollisionConfiguration();
            m_collision_dispatcher    = new btCollisionDispatcher(m_collision_configuration);
            m_world                   = new btDiscreteDynamicsWorld(m_collision_dispatcher, m_broadphase, m_constraint_solver, m_collision_configuration);
        }

        // Setup
        m_world->setGravity(ToBtVector3(m_gravity));
        m_world->getDispatchInfo().m_useContinuous = true;
        m_world->getSolverInfo().m_splitImpulse = false;
        m_world->getSolverInfo().m_numIterations = m_max_solve_iterations;
    }

    Physics::~Physics()
    {
        delete m_world;
        delete m_constraint_solver;
        delete m_collision_dispatcher;
        delete m_collision_configuration;
        delete m_broadphase;
        delete m_world_info;
        delete m_debug_draw;
    }

    void Physics::OnInitialise()
    {
        // Get dependencies
        m_renderer = m_context->GetSystem<Renderer>();
        m_profiler = m_context->GetSystem<Profiler>();

        // Get version
        const auto major = to_string(btGetVersion() / 100);
        const auto minor = to_string(btGetVersion()).erase(0, 1);
        Settings::RegisterThirdPartyLib("Bullet", major + "." + minor, "https://github.com/bulletphysics/bullet3");

        // Enabled debug drawing
        {
            m_debug_draw = new PhysicsDebugDraw(m_renderer);

            if (m_world)
            {
                m_world->setDebugDrawer(m_debug_draw);
            }
        }
    }

    void Physics::OnTick(double delta_time_sec)
    {
        if (!m_world)
            return;
        
        // Debug draw
        if (m_renderer->GetOption<bool>(RendererOption::Debug_Physics))
        {
            m_world->debugDrawWorld();
        }

        // Don't simulate physics if they are turned off or we are not in game mode
        if (!m_context->m_engine->IsFlagSet(EngineMode::Physics) || !m_context->m_engine->IsFlagSet(EngineMode::Game))
            return;

        SP_SCOPED_TIME_BLOCK(m_profiler);

        // Picking
        {
            if (m_context->GetSystem<Input>()->GetKeyDown(KeyCode::Click_Left) && m_context->GetSystem<Input>()->GetMouseIsInViewport())
            {
                PickBody();
            }
            else if (m_context->GetSystem<Input>()->GetKeyUp(KeyCode::Click_Left))
            {
                UnpickBody();
            }

            MovePickedBody();
        }

        // This equation must be met: timeStep < maxSubSteps * fixedTimeStep
        auto internal_time_step = 1.0f / m_internal_fps;
        auto max_substeps       = static_cast<int>(delta_time_sec * m_internal_fps) + 1;
        if (m_max_sub_steps < 0)
        {
            internal_time_step  = static_cast<float>(delta_time_sec);
            max_substeps        = 1;
        }
        else if (m_max_sub_steps > 0)
        {
            max_substeps = Helper::Min(max_substeps, m_max_sub_steps);
        }

        // Step the physics world. 
        m_simulating = true;
        m_world->stepSimulation(static_cast<float>(delta_time_sec), max_substeps, internal_time_step);
        m_simulating = false;
    }

    void Physics::AddBody(btRigidBody* body) const
    {
        if (!m_world)
            return;

        m_world->addRigidBody(body);
    }

    void Physics::RemoveBody(btRigidBody*& body) const
    {
        if (!m_world)
            return;

        m_world->removeRigidBody(body);
        delete body->getMotionState();
        delete body;
    }

    void Physics::AddConstraint(btTypedConstraint* constraint, bool collision_with_linked_body /*= true*/) const
    {
        if (!m_world)
            return;

        m_world->addConstraint(constraint, !collision_with_linked_body);
    }

    void Physics::RemoveConstraint(btTypedConstraint*& constraint) const
    {
        if (!m_world)
            return;

        m_world->removeConstraint(constraint);
        delete constraint;
    }

    void Physics::AddBody(btSoftBody* body) const
    {
        if (!m_world)
            return;

        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->addSoftBody(body);
        }
    }

    void Physics::RemoveBody(btSoftBody*& body) const
    {
        if (btSoftRigidDynamicsWorld* world = static_cast<btSoftRigidDynamicsWorld*>(m_world))
        {
            world->removeSoftBody(body);
            delete body;
        }
    }

    Vector3 Physics::GetGravity() const
    {
        auto gravity = m_world->getGravity();
        if (!gravity)
        {
            SP_LOG_ERROR("Unable to get gravity, ensure physics are properly initialized.");
            return Vector3::Zero;
        }
        return gravity ? ToVector3(gravity) : Vector3::Zero;
    }

    void Physics::PickBody()
    {
        if (shared_ptr<Camera> camera = m_renderer->GetCamera())
        {
            const Ray& picking_ray = camera->GetPickingRay();

            if (picking_ray.IsDefined())
            { 
                // Get camera picking ray
                Vector3 ray_start     = picking_ray.GetStart();
                Vector3 ray_direction = picking_ray.GetDirection();
                Vector3 ray_end       = ray_start + ray_direction * camera->GetFarPlane();

                btVector3 bt_ray_start = ToBtVector3(ray_start);
                btVector3 bt_ray_end   = ToBtVector3(ray_end);
                btCollisionWorld::ClosestRayResultCallback rayCallback(bt_ray_start, bt_ray_end);

                rayCallback.m_flags |= btTriangleRaycastCallback::kF_UseGjkConvexCastRaytest;
                m_world->rayTest(bt_ray_start, bt_ray_end, rayCallback);

                if (rayCallback.hasHit())
                {
                    btVector3 pick_position = rayCallback.m_hitPointWorld;

                    if (btRigidBody* body = (btRigidBody*)btRigidBody::upcast(rayCallback.m_collisionObject))
                    {
                        if (!(body->isStaticObject() || body->isKinematicObject()))
                        {
                            m_picked_body = body;
                            m_activation_state = m_picked_body->getActivationState();
                            m_picked_body->setActivationState(DISABLE_DEACTIVATION);
                            btVector3 localPivot = body->getCenterOfMassTransform().inverse() * pick_position;
                            btPoint2PointConstraint* p2p = new btPoint2PointConstraint(*body, localPivot);
                            m_world->addConstraint(p2p, true);
                            m_picked_constraint = p2p;
                            btScalar mousePickClamping = 30.f;
                            p2p->m_setting.m_impulseClamp = mousePickClamping;
                            p2p->m_setting.m_tau = 0.001f; // very weak constraint for picking
                        }
                    }

                    m_picking_position_previous = ray_end;
                    m_hit_position              = ToVector3(pick_position);
                    m_picking_distance_previous = (m_hit_position - ray_start).Length();

                    m_renderer->DrawSphere(m_hit_position, 0.1f, 32);
                }
            }
        }
    }

    void Physics::UnpickBody()
    {
        if (m_picked_constraint)
        {
            m_picked_body->forceActivationState(m_activation_state);
            m_picked_body->activate();
            m_world->removeConstraint(m_picked_constraint);
            delete m_picked_constraint;
            m_picked_constraint = nullptr;
            m_picked_body = nullptr;
        }
    }

    void Physics::MovePickedBody()
    {
        if (shared_ptr<Camera> camera = m_renderer->GetCamera())
        {
            Ray picking_ray       = camera->ComputePickingRay();
            Vector3 ray_start     = picking_ray.GetStart();
            Vector3 ray_direction = picking_ray.GetDirection();

            if (m_picked_body && m_picked_constraint)
            {
                if (btPoint2PointConstraint* pick_constraint = static_cast<btPoint2PointConstraint*>(m_picked_constraint))
                {
                    // keep it at the same picking distance
                    ray_direction *= m_picking_distance_previous;
                    Vector3 new_pivot_b = ray_start + ray_direction;
                    pick_constraint->setPivotB(ToBtVector3(new_pivot_b));
                }
            }
        }
    }
}
