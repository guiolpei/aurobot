// Standard
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <math.h>
#include <cmath>
#include <Eigen/Geometry>

// ROS
#include <ros/topic.h>
#include <tf/transform_listener.h>
#include <tf_conversions/tf_eigen.h>
#include <pcl_ros/transforms.h>
#include <pcl_conversions/pcl_conversions.h>

// MoveIt!
#include <moveit/move_group_interface/move_group_interface.h>
#include <moveit/planning_scene_interface/planning_scene_interface.h>
#include <rviz_visual_tools/rviz_visual_tools.h>

// Custom
#include <geograsp/GraspConfigMsg.h>

//
// CONST VARIABLES
//

const std::string CAMERA_FRAME = "/top_camera_link";

const std::string MIDDLE_PLANNING_GROUP = "rh_middle_finger";
const std::string MIDDLE_END_EFFECTOR_LINK = "rh_mftip";
const std::string MIDDLE_MOVING_JOINT = "rh_MFJ3";

const std::string THUMB_PLANNING_GROUP = "rh_thumb";
const std::string THUMB_END_EFFECTOR_LINK = "rh_thtip";
const std::string THUMB_MOVING_JOINT = "rh_THJ5";

const std::string SHADOW_HAND_PLANNING_GROUP = "right_hand";
const std::string ARM_PLANNING_GROUP = "r_arm";
const std::string ARM_END_EFFECTOR_LINK = "rh_forearm";

const std::string PREGRASP_NAMED_TARGET = "pregrasp-mf";

const std::string COLLISION_OBJECT_ID = "grasping_object";

const std::string GRASP_CONFIG_TOPIC = "/geograsp/grasp_config";

const char REPEAT_PLAN = 'r';

const float CLOSE_FACTOR = 0.9;

//
//  AUXILIAR POINTS TRANSFORMER FUNCTION
//
//  This function transform a given point from a source frame to a target frame.
//

Eigen::Vector3d transformPoint(const tf::Stamped<tf::Point> & tfPointIn, 
    const std::string & sourceFrame, const std::string & targetFrame) {
  tf::TransformListener transformer;
  tf::Stamped<tf::Point> tfPointOut;

  transformer.waitForTransform(targetFrame, sourceFrame, ros::Time(0), ros::Duration(3.0));
  transformer.transformPoint(targetFrame, tfPointIn, tfPointOut);

  Eigen::Vector3d outputPoint(tfPointOut.getX(), tfPointOut.getY(), tfPointOut.getZ());

  return outputPoint;
}

Eigen::Vector3d transformVector(const tf::Stamped<tf::Vector3> & tfVectorIn, 
    const std::string & sourceFrame, const std::string & targetFrame) {
  tf::TransformListener transformer;
  tf::Stamped<tf::Vector3> tfVectorOut;

  transformer.waitForTransform(targetFrame, sourceFrame, ros::Time(0), ros::Duration(3.0));
  transformer.transformVector(targetFrame, tfVectorIn, tfVectorOut);

  Eigen::Vector3d outputVector(tfVectorOut.getX(), tfVectorOut.getY(), tfVectorOut.getZ());

  return outputVector;
}

//
//  AUXILIAR JOINTS POSITION FINDING FUNCTION
//
//  This function holds the relationship between a certain tips width and 
//  the joints of the Shadow.
//

double getThumbJointPosition(double width) {
  double a = -93.4217162843, b = 22.721646641, c = -7.6878665291, d = 0.7746088563;
  return a * std::pow(width, 3) + b  * std::pow(width, 2) + c * width + d;
}

double getMiddleJointPosition(double width) {
  double a = -105.1326116902, b = 25.5699225863, c = -8.6515803678, d = 1.0183100834;
  return a * std::pow(width, 3) + b  * std::pow(width, 2) + c * width + d;
}



//
//  AUXILIAR GRASPING FUNCTION
//
//  These functions positions the fingers regarding the object's width.
//

bool moveShadowPregrasp() {
  moveit::planning_interface::MoveGroupInterface shadowMoveGroup(SHADOW_HAND_PLANNING_GROUP);
  moveit::planning_interface::MoveGroupInterface::Plan shadowMovePlan;
  bool planSuccess = false;
  
  shadowMoveGroup.setJointValueTarget(shadowMoveGroup.getNamedTargetValues(PREGRASP_NAMED_TARGET));
  planSuccess = shadowMoveGroup.plan(shadowMovePlan);

  if(!planSuccess){
    std::cout << "[ERROR] Fingers planning for joint poisition failed!\n";
    return false;
  }

  shadowMoveGroup.execute(shadowMovePlan);

  return true;
}

bool moveShadowGrasp(double objectWidth) {
  moveit::planning_interface::MoveGroupInterface shadowMoveGroup(SHADOW_HAND_PLANNING_GROUP);
  moveit::planning_interface::MoveGroupInterface::Plan shadowMovePlan;
  std::map<std::string, double> graspJoints;
  bool planSuccess = false;
  double middleJointPosition = getMiddleJointPosition(objectWidth);
  double thumbJointPosition = getThumbJointPosition(objectWidth);

  graspJoints = shadowMoveGroup.getNamedTargetValues(PREGRASP_NAMED_TARGET);
  graspJoints[MIDDLE_MOVING_JOINT] = middleJointPosition;
  graspJoints[THUMB_MOVING_JOINT] = thumbJointPosition;

  shadowMoveGroup.setJointValueTarget(graspJoints);
  //shadowMoveGroup.setMaxVelocityScalingFactor(0.50);
  planSuccess = shadowMoveGroup.plan(shadowMovePlan);

  if(!planSuccess){
    std::cout << "[ERROR] Fingers planning for joint poisition failed!\n";
    return false;
  }

  shadowMoveGroup.execute(shadowMovePlan);

  return true;
}


Eigen::Vector3d computeMidPoint() {
  // Middle finger
  moveit::planning_interface::MoveGroupInterface middleMoveGroup(MIDDLE_PLANNING_GROUP);
  geometry_msgs::PoseStamped middleCurrentPose = middleMoveGroup.getCurrentPose(MIDDLE_END_EFFECTOR_LINK);
  Eigen::Vector3d middlePoint(middleCurrentPose.pose.position.x, middleCurrentPose.pose.position.y,
    middleCurrentPose.pose.position.z);

  // Thumb finger
  moveit::planning_interface::MoveGroupInterface thumbMoveGroup(THUMB_PLANNING_GROUP);
  geometry_msgs::PoseStamped thumbCurrentPose = thumbMoveGroup.getCurrentPose(THUMB_END_EFFECTOR_LINK);
  Eigen::Vector3d thumbPoint(thumbCurrentPose.pose.position.x, thumbCurrentPose.pose.position.y,
    thumbCurrentPose.pose.position.z);

  Eigen::Vector3d graspMid((middlePoint[0] + thumbPoint[0]) / 2.0,
    (middlePoint[1] + thumbPoint[1]) / 2.0, (middlePoint[2] + thumbPoint[2]) / 2.0);

  return graspMid;
}

Eigen::Affine3d computeMidPointPose(rviz_visual_tools::RvizVisualToolsPtr visualTools) {
  // Middle finger
  moveit::planning_interface::MoveGroupInterface middleMoveGroup(MIDDLE_PLANNING_GROUP);
  geometry_msgs::PoseStamped middleCurrentPose = middleMoveGroup.getCurrentPose(MIDDLE_END_EFFECTOR_LINK);
  Eigen::Vector3d middlePoint(middleCurrentPose.pose.position.x, middleCurrentPose.pose.position.y,
    middleCurrentPose.pose.position.z);

  // Thumb finger
  moveit::planning_interface::MoveGroupInterface thumbMoveGroup(THUMB_PLANNING_GROUP);
  geometry_msgs::PoseStamped thumbCurrentPose = thumbMoveGroup.getCurrentPose(THUMB_END_EFFECTOR_LINK);
  Eigen::Vector3d thumbPoint(thumbCurrentPose.pose.position.x, thumbCurrentPose.pose.position.y,
    thumbCurrentPose.pose.position.z);

  // Grasp Mid Point
  Eigen::Vector3d graspMid((middlePoint[0] + thumbPoint[0]) / 2.0,
    (middlePoint[1] + thumbPoint[1]) / 2.0, (middlePoint[2] + thumbPoint[2]) / 2.0);

  // Arrenged for the shadow pose
  tf::Stamped<tf::Vector3> handYIn;
  handYIn.setX(0);
  handYIn.setY(-1);
  handYIn.setZ(0);
  handYIn.frame_id_ = ARM_END_EFFECTOR_LINK;
  Eigen::Vector3d handY = transformVector(handYIn, ARM_END_EFFECTOR_LINK, "/world");

  Eigen::Vector3d axeX, axeY, axeZ, axeAux;

  axeZ = middlePoint - thumbPoint;
  axeX = axeZ.cross(handY);
  axeY = axeZ.cross(axeX);
  
  axeX.normalize();
  axeY.normalize();
  axeZ.normalize();    

  Eigen::Affine3d midPointPose;
  Eigen::Matrix3d midPointRotation;
  midPointRotation << axeX[0], axeY[0], axeZ[0], 
                      axeX[1], axeY[1], axeZ[1],
                      axeX[2], axeY[2], axeZ[2];
  midPointPose.translation() = graspMid;
  midPointPose.linear() = midPointRotation;

  visualTools->publishAxis(midPointPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  return midPointPose;
}


void drawReferencePoints(rviz_visual_tools::RvizVisualToolsPtr visualTools){
  // Middle finger
  moveit::planning_interface::MoveGroupInterface middleMoveGroup(MIDDLE_PLANNING_GROUP);
  geometry_msgs::PoseStamped middleCurrentPose = middleMoveGroup.getCurrentPose(MIDDLE_END_EFFECTOR_LINK);
  Eigen::Vector3d middlePoint(middleCurrentPose.pose.position.x, middleCurrentPose.pose.position.y,
    middleCurrentPose.pose.position.z);

  // Thumb finger
  moveit::planning_interface::MoveGroupInterface thumbMoveGroup(THUMB_PLANNING_GROUP);
  geometry_msgs::PoseStamped thumbCurrentPose = thumbMoveGroup.getCurrentPose(THUMB_END_EFFECTOR_LINK);
  Eigen::Vector3d thumbPoint(thumbCurrentPose.pose.position.x, thumbCurrentPose.pose.position.y,
    thumbCurrentPose.pose.position.z);

  // Midpoint between thumb and middle finger
  Eigen::Vector3d graspMid((middlePoint[0] + thumbPoint[0]) / 2.0,
    (middlePoint[1] + thumbPoint[1]) / 2.0, (middlePoint[2] + thumbPoint[2]) / 2.0);  

  visualTools->publishSphere(middlePoint, rviz_visual_tools::BLUE, rviz_visual_tools::LARGE);
  visualTools->publishSphere(thumbPoint, rviz_visual_tools::RED, rviz_visual_tools::LARGE);
  visualTools->publishSphere(graspMid, rviz_visual_tools::YELLOW, rviz_visual_tools::LARGE);
  visualTools->trigger();
}



//
//  GRASPER FUNCTION
//
//  This function executes the whole grasping process given a tuple of contact points.
//  The process is the following:
//  - Transform the contact points to the /world frame
//  - Preprosition the robotic fingers to a pre grasping position
//  - Calculate the arm pose regarding the object orientation and the contact points
//  - Move the arm to such pose and close the fingers
//

void planGrasp(const geograsp::GraspConfigMsgConstPtr & inputGrasp, const std::string & collisionId) {
  rviz_visual_tools::RvizVisualToolsPtr visualTools;
  visualTools.reset(new rviz_visual_tools::RvizVisualTools("/world", "/rviz_visual_markers"));
  visualTools->trigger();
  moveit::planning_interface::MoveGroupInterface armMoveGroup(ARM_PLANNING_GROUP);

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Transform the coordinates from the camera frame to the world

  tf::Stamped<tf::Point> firstPointIn, secondPointIn, objAxisCenterIn;
  firstPointIn.setX(inputGrasp->first_point_x);
  firstPointIn.setY(inputGrasp->first_point_y);
  firstPointIn.setZ(inputGrasp->first_point_z);
  firstPointIn.frame_id_ = CAMERA_FRAME;
  secondPointIn.setX(inputGrasp->second_point_x);
  secondPointIn.setY(inputGrasp->second_point_y);
  secondPointIn.setZ(inputGrasp->second_point_z);
  secondPointIn.frame_id_ = CAMERA_FRAME;
  objAxisCenterIn.setX(inputGrasp->obj_axis_coeff_0);
  objAxisCenterIn.setY(inputGrasp->obj_axis_coeff_1);
  objAxisCenterIn.setZ(inputGrasp->obj_axis_coeff_2);
  objAxisCenterIn.frame_id_ = CAMERA_FRAME;

  Eigen::Vector3d firstPoint = transformPoint(firstPointIn, CAMERA_FRAME, "/world");
  Eigen::Vector3d secondPoint = transformPoint(secondPointIn, CAMERA_FRAME, "/world");
  Eigen::Vector3d objAxisCenter = transformPoint(objAxisCenterIn, CAMERA_FRAME, "/world");

  tf::Stamped<tf::Vector3> objAxisVectorIn;
  objAxisVectorIn.setX(inputGrasp->obj_axis_coeff_3);
  objAxisVectorIn.setY(inputGrasp->obj_axis_coeff_4);
  objAxisVectorIn.setZ(inputGrasp->obj_axis_coeff_5);
  objAxisVectorIn.frame_id_ = CAMERA_FRAME;

  Eigen::Vector3d objAxisVector = transformVector(objAxisVectorIn, CAMERA_FRAME, "/world");

  visualTools->publishSphere(firstPoint, rviz_visual_tools::BLUE, rviz_visual_tools::LARGE);
  visualTools->publishSphere(secondPoint, rviz_visual_tools::RED, rviz_visual_tools::LARGE);
  visualTools->publishSphere(objAxisCenter, rviz_visual_tools::GREY, rviz_visual_tools::LARGE);
  visualTools->publishLine(objAxisCenter, objAxisCenter + objAxisVector, rviz_visual_tools::GREY,
    rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Read the object's cloud message, transform it and get the boundary coordinates

  sensor_msgs::PointCloud2 objectCloudMsgOut, objectcloudsMsgIn = inputGrasp->object_cloud;
  tf::TransformListener tfListener;
  tf::StampedTransform transform;

  tfListener.waitForTransform("/world", CAMERA_FRAME, ros::Time(0), ros::Duration(3.0));
  tfListener.lookupTransform("/world", CAMERA_FRAME, ros::Time(0), transform);
  pcl_ros::transformPointCloud("/world", transform, objectcloudsMsgIn, objectCloudMsgOut);

  pcl::PointCloud<pcl::PointXYZRGB>::Ptr objectCloud(new pcl::PointCloud<pcl::PointXYZRGB>());
  pcl::fromROSMsg<pcl::PointXYZRGB>(objectCloudMsgOut, *objectCloud);

  std::cout << "Object's cloud size: " << objectCloud->width * objectCloud->height << "\n";

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Place gripper joints for reaching

  double pointsDistance = std::sqrt(std::pow(firstPoint[0] - secondPoint[0], 2) + 
      std::pow(firstPoint[1] - secondPoint[1], 2) + std::pow(firstPoint[2] - secondPoint[2], 2));

  std::cout << "Points distance: " << pointsDistance << "\n";
  
  if (!moveShadowGrasp(pointsDistance)){
    std::cout << "[ERROR] Fingers movement for pregrasp pose failed!\n";
    return;
  }

  drawReferencePoints(visualTools);
  ROS_INFO("[AUROBOT] FINGERS POSITIONED IN PRE GRASPING POSE");
   
  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Calculate grasper position and orientation

  // Get the mid point and pose in the object wrt WORLD
  Eigen::Vector3d midPoint((firstPoint[0] + secondPoint[0]) / 2.0,
    (firstPoint[1] + secondPoint[1]) / 2.0, (firstPoint[2] + secondPoint[2]) / 2.0);

  visualTools->publishSphere(midPoint, rviz_visual_tools::GREEN, rviz_visual_tools::LARGE);
  visualTools->trigger();

  Eigen::Vector3d axeX, axeY, axeZ, axeAux;
  
  // Arrenged for the shadow hand
  axeZ = firstPoint - secondPoint;
  axeX = axeZ.cross(objAxisVector);
  axeY = axeZ.cross(axeX);

  axeAux = axeY;
  axeY = axeX;
  axeX = -axeAux;
  
  axeX.normalize();
  axeY.normalize();
  axeZ.normalize();    

  Eigen::Affine3d midPointPose;
  Eigen::Matrix3d midPointRotation;
  midPointRotation << axeX[0], axeY[0], axeZ[0], 
                      axeX[1], axeY[1], axeZ[1],
                      axeX[2], axeY[2], axeZ[2];
  midPointPose.translation() = midPoint;
  midPointPose.linear() = midPointRotation;

  visualTools->publishAxis(midPointPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // Compute pose of the midpoint wrt EE (Shadow)
  Eigen::Affine3d shadowMidPointPose = computeMidPointPose(visualTools);

  tf::Stamped<tf::Vector3> poseAxeXIn;
  poseAxeXIn.setX(shadowMidPointPose.linear()(0, 0));
  poseAxeXIn.setY(shadowMidPointPose.linear()(1, 0));
  poseAxeXIn.setZ(shadowMidPointPose.linear()(2, 0));
  poseAxeXIn.frame_id_ = "/world";
  Eigen::Vector3d poseAxeXEE = transformVector(poseAxeXIn, "/world", ARM_END_EFFECTOR_LINK);

  tf::Stamped<tf::Vector3> poseAxeYIn;
  poseAxeYIn.setX(shadowMidPointPose.linear()(0, 1));
  poseAxeYIn.setY(shadowMidPointPose.linear()(1, 1));
  poseAxeYIn.setZ(shadowMidPointPose.linear()(2, 1));
  poseAxeYIn.frame_id_ = "/world";
  Eigen::Vector3d poseAxeYEE = transformVector(poseAxeYIn, "/world", ARM_END_EFFECTOR_LINK);

  tf::Stamped<tf::Vector3> poseAxeZIn;
  poseAxeZIn.setX(shadowMidPointPose.linear()(0, 2));
  poseAxeZIn.setY(shadowMidPointPose.linear()(1, 2));
  poseAxeZIn.setZ(shadowMidPointPose.linear()(2, 2));
  poseAxeZIn.frame_id_ = "/world";
  Eigen::Vector3d poseAxeZEE = transformVector(poseAxeZIn, "/world", ARM_END_EFFECTOR_LINK);

  Eigen::Affine3d shadowMidPointPoseEE;
  Eigen::Matrix3d shadowMidPointRotationEE;
  shadowMidPointRotationEE << poseAxeXEE[0], poseAxeYEE[0], poseAxeZEE[0], 
                              poseAxeXEE[1], poseAxeYEE[1], poseAxeZEE[1],
                              poseAxeXEE[2], poseAxeYEE[2], poseAxeZEE[2];
  shadowMidPointPoseEE.linear() = shadowMidPointRotationEE;

  Eigen::Affine3d graspPose = midPointPose * shadowMidPointPoseEE.inverse();;

  // Compute translation of the EE wrt object's midpoint with graspPose
  tf::Stamped<tf::Point> poseMidIn;
  poseMidIn.setX(shadowMidPointPose.translation()[0]);
  poseMidIn.setY(shadowMidPointPose.translation()[1]);
  poseMidIn.setZ(shadowMidPointPose.translation()[2]);
  poseMidIn.frame_id_ = "/world";

  Eigen::Vector3d poseMidEE = transformPoint(poseMidIn, "/world", ARM_END_EFFECTOR_LINK);
  Eigen::Vector3d graspTransEE(-poseMidEE[0], -poseMidEE[1], -poseMidEE[2]);
  tf::Transform graspPoseTransform;
  tf::Vector3 transPointTF, transPointTFed;

  tf::transformEigenToTF(graspPose, graspPoseTransform);
  tf::vectorEigenToTF(graspTransEE, transPointTF);

  transPointTFed = graspPoseTransform(transPointTF);

  tf::vectorTFToEigen(transPointTFed, graspTransEE);

  graspPose.translation() = graspTransEE;

  visualTools->publishSphere(graspPose.translation(), rviz_visual_tools::PINK, rviz_visual_tools::LARGE);
  visualTools->publishAxis(graspPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // Compute pre-grasp translation
  Eigen::Vector3d pregraspTransEE(-poseMidEE[0], -poseMidEE[1] + 0.1, -poseMidEE[2]);
  tf::Vector3 pregraspPointTF, pregraspPointTFed;

  tf::vectorEigenToTF(pregraspTransEE, pregraspPointTF);

  pregraspPointTFed = graspPoseTransform(pregraspPointTF);

  tf::vectorTFToEigen(pregraspPointTFed, pregraspTransEE);

  Eigen::Affine3d pregraspPose = graspPose;
  pregraspPose.translation() = pregraspTransEE;

  visualTools->publishSphere(pregraspPose.translation(), rviz_visual_tools::ORANGE, rviz_visual_tools::LARGE);
  visualTools->publishAxis(pregraspPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // Pregrasp pose with wolrd Z
  Eigen::Vector3d preworldTrans = graspTransEE + Eigen::Vector3d(0, 0, 0.10);
  Eigen::Affine3d preworldPose = graspPose;
  preworldPose.translation() = preworldTrans;

  visualTools->publishSphere(preworldTrans, rviz_visual_tools::WHITE, rviz_visual_tools::LARGE);
  visualTools->publishAxis(preworldPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // Postgrasp pose is the grasp pose but a litle bit upwards, following the world's Z
  Eigen::Vector3d postgraspTrans = graspTransEE + Eigen::Vector3d(0, 0, 0.20);
  Eigen::Affine3d postgraspPose = graspPose;
  postgraspPose.translation() = postgraspTrans;

  visualTools->publishSphere(postgraspTrans, rviz_visual_tools::WHITE, rviz_visual_tools::LARGE);
  visualTools->publishAxis(postgraspPose, rviz_visual_tools::MEDIUM);
  visualTools->trigger();

  // Prepare fingers to pre-grasp position
  moveShadowPregrasp();

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Moving arm and close grasp

  moveit::planning_interface::PlanningSceneInterface planningSceneInterface;
  moveit::planning_interface::MoveGroupInterface::Plan armPlan;
  bool successArmPlan = false;
  char inputChar = REPEAT_PLAN;

  std::cout << armMoveGroup.getEndEffectorLink() << "\n";

  armMoveGroup.setPoseTarget(preworldPose); //(pregraspPose);
  //TRRTkConfigDefault //RRTConnectkConfigDefault // Lento! PRMstarkConfigDefault
  armMoveGroup.setPlannerId("TRRTkConfigDefault");
  armMoveGroup.setPlanningTime(5.0);
  armMoveGroup.setNumPlanningAttempts(20);
  //armMoveGroup.setMaxVelocityScalingFactor(0.50);
  //armMoveGroup.setMaxAccelerationScalingFactor(0.50);

  do {
    ROS_INFO("[AUROBOT] PLANNING PREGRASP POSITION");

    successArmPlan = armMoveGroup.plan(armPlan);
    std::cin >> inputChar;
  } while(inputChar == REPEAT_PLAN);

  ROS_INFO("Arm pregrasp plan %s", successArmPlan ? "SUCCEED" : "FAILED");

  if (successArmPlan) { // Successful plan for the pre grasping arm position
    std::cout << "PRESS ENTER TO MOVE TO PREGRASPING POSE\n";
    std::getchar();

    armMoveGroup.execute(armPlan);
    ROS_INFO("[AUROBOT] ARM POSITIONED IN PREGRASPING POSE");

    // Remove collision object, since we want to get close to it
    std::vector<std::string> objectId;
    objectId.push_back(collisionId);
    planningSceneInterface.removeCollisionObjects(objectId);

    armMoveGroup.setPoseTarget(graspPose);

    do {
      ROS_INFO("[AUROBOT] PLANNING GRASP POSITION");

      successArmPlan = armMoveGroup.plan(armPlan);
      std::cin >> inputChar;
    } while(inputChar == REPEAT_PLAN);

    ROS_INFO("Arm grasp plan %s", successArmPlan ? "SUCCEED" : "FAILED");

    if (successArmPlan) { // Successful plan for the grasping arm position
      std::cout << "PRESS ENTER TO MOVE TO GRASPING POSE\n";
      std::getchar();

      armMoveGroup.execute(armPlan);
      ROS_INFO("[AUROBOT] ARM POSITIONED IN GRASPING POSE");
      
      std::cout << "PRESS ENTER TO GRASP\n";
      std::getchar();

      if (!moveShadowGrasp(pointsDistance * CLOSE_FACTOR) && !moveShadowGrasp(pointsDistance))
        std::cout << "[ERROR] Fingers movement for closing grasp failed!\n";

      ROS_INFO("[AUROBOT] GRASP COMPLETED");

      armMoveGroup.setPoseTarget(postgraspPose);

      do {
        ROS_INFO("[AUROBOT] PLANNING POSTGRASP POSITION");

        successArmPlan = armMoveGroup.plan(armPlan);
        std::cin >> inputChar;
      } while(inputChar == REPEAT_PLAN);

      ROS_INFO("Arm grasp plan %s", successArmPlan ? "SUCCEED" : "FAILED");

      if (successArmPlan) { // Successful plan for the post grasping arm position
        std::cout << "PRESS ENTER TO MOVE TO POSTGRASPING POSE\n";
        std::getchar();

        armMoveGroup.execute(armPlan);
      }
    }
  }

  ros::shutdown();
}



//
//  SCENE PROCESSING FUNCTION
//
//  This module is in charge of publishing collision objects by reading the scene input.
//  It calls the grasper main function with the closest object.
//

void processGraspMsg(const geograsp::GraspConfigMsgConstPtr & graspConfig) {
  moveit::planning_interface::PlanningSceneInterface planningSceneInterface;
  std::string objCollId = "0";

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Read the object's cloud message, transform it and get the boundary coordinates

  sensor_msgs::PointCloud2 objectCloudMsgOut, objectcloudsMsgIn = graspConfig->object_cloud;
  tf::TransformListener tfListener;
  tf::StampedTransform transform;

  tfListener.waitForTransform("/world", CAMERA_FRAME, ros::Time(0), ros::Duration(3.0));
  tfListener.lookupTransform("/world", CAMERA_FRAME, ros::Time(0), transform);
  pcl_ros::transformPointCloud("/world", transform, objectcloudsMsgIn, objectCloudMsgOut);

  pcl::PointCloud<pcl::PointXYZRGB>::Ptr objectCloud(new pcl::PointCloud<pcl::PointXYZRGB>());
  pcl::fromROSMsg<pcl::PointXYZRGB>(objectCloudMsgOut, *objectCloud);

  std::cout << "Object's cloud size: " << objectCloud->width * objectCloud->height << "\n";

  float minX, minY, minZ, maxX, maxY, maxZ;
  minX = minY = minZ = std::numeric_limits<float>::max();
  maxX = maxY = maxZ = -std::numeric_limits<float>::max();

  for (size_t i = 0; i < objectCloud->points.size(); ++i) {
    if (objectCloud->points[i].x < minX)
      minX = objectCloud->points[i].x;
    if (objectCloud->points[i].x > maxX)
      maxX = objectCloud->points[i].x;

    if (objectCloud->points[i].y < minY)
      minY = objectCloud->points[i].y;
    if (objectCloud->points[i].y > maxY)
      maxY = objectCloud->points[i].y;

    if (objectCloud->points[i].z < minZ)
      minZ = objectCloud->points[i].z;
    if (objectCloud->points[i].z > maxZ)
      maxZ = objectCloud->points[i].z;
  }

  Eigen::Vector3d minBoundingPoint(minX, minY, minZ), maxBoundingPoint(maxX, maxY, maxZ);

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = 
  // Add the cloud as a collision object

  moveit_msgs::CollisionObject collisionObject;
  collisionObject.header.frame_id = "/world";

  // The id of the object is used to identify it.
  collisionObject.id = objCollId;

  // Define a bounding box
  shape_msgs::SolidPrimitive primitive;
  primitive.type = primitive.BOX;
  primitive.dimensions.resize(3);
  // Scaled a little bit down so the bounding box does not exceed the real object boundaries
  primitive.dimensions[0] = 0.7 * std::abs(minBoundingPoint[0] - maxBoundingPoint[0]);
  primitive.dimensions[1] = 0.7 * std::abs(minBoundingPoint[1] - maxBoundingPoint[1]);
  primitive.dimensions[2] = 0.7 * std::abs(minBoundingPoint[2] - maxBoundingPoint[2]);

  //Define a pose for the box (specified relative to frame_id)
  geometry_msgs::Pose boxPose;
  boxPose.orientation.w = 1.0; // TODO: USE OBJECT'S AXIS?
  boxPose.position.x = (minBoundingPoint[0] + maxBoundingPoint[0]) / 2.0;
  boxPose.position.y = (minBoundingPoint[1] + maxBoundingPoint[1]) / 2.0;
  boxPose.position.z = (minBoundingPoint[2] + maxBoundingPoint[2]) / 2.0;

  collisionObject.primitives.push_back(primitive);
  collisionObject.primitive_poses.push_back(boxPose);
  collisionObject.operation = collisionObject.ADD;

  // Now, let's add the collision object into the world
  ROS_INFO("[AUROBOT] Add collision object into the world");
  planningSceneInterface.applyCollisionObject(collisionObject);

  std::cout << "Planning grasp to object ...\n";
  planGrasp(graspConfig, objCollId);
}


//
//  MAIN FUNCTION
//
//  Waits for the last published message with the contact points and passes it to the
//  function in charge of planning and moving the hand to them.
//

int main(int argc, char **argv) {
  ros::init(argc, argv, "shadow_plan_grasp");
  ros::NodeHandle node_handle;
  ros::AsyncSpinner spinner(1);
  spinner.start();
  
  std::cout << "Empezando...\n";

  geograsp::GraspConfigMsgConstPtr receivedMessage = 
    ros::topic::waitForMessage<geograsp::GraspConfigMsg>(GRASP_CONFIG_TOPIC);
  processGraspMsg(receivedMessage);

  std::cout << "Cerrando...\n";

  return 0;
}