/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CAL_BUTTON_Pin GPIO_PIN_13
#define CAL_BUTTON_GPIO_Port GPIOC
#define STATUS_LED_Pin GPIO_PIN_2
#define STATUS_LED_GPIO_Port GPIOF
#define LEFT_MOTOR_CS_Pin GPIO_PIN_0
#define LEFT_MOTOR_CS_GPIO_Port GPIOA
#define RIGHT_MOTOR_CS_Pin GPIO_PIN_1
#define RIGHT_MOTOR_CS_GPIO_Port GPIOA
#define STEERING_MOTOR_CS_Pin GPIO_PIN_2
#define STEERING_MOTOR_CS_GPIO_Port GPIOA
#define STEERING_MOTOR_DIR_Pin GPIO_PIN_14
#define STEERING_MOTOR_DIR_GPIO_Port GPIOB
#define LEFT_MOTOR_SLP_Pin GPIO_PIN_8
#define LEFT_MOTOR_SLP_GPIO_Port GPIOD
#define RIGHT_MOTOR_SLP_Pin GPIO_PIN_9
#define RIGHT_MOTOR_SLP_GPIO_Port GPIOD
#define STEERING_MOTOR_SLP_Pin GPIO_PIN_10
#define STEERING_MOTOR_SLP_GPIO_Port GPIOD
#define LEFT_MOTOR_FLT_Pin GPIO_PIN_11
#define LEFT_MOTOR_FLT_GPIO_Port GPIOD
#define LEFT_MOTOR_FLT_EXTI_IRQn EXTI15_10_IRQn
#define RIGHT_MOTOR_FLT_Pin GPIO_PIN_12
#define RIGHT_MOTOR_FLT_GPIO_Port GPIOD
#define RIGHT_MOTOR_FLT_EXTI_IRQn EXTI15_10_IRQn
#define STEERING_MOTOR_FLT_Pin GPIO_PIN_13
#define STEERING_MOTOR_FLT_GPIO_Port GPIOD
#define STEERING_MOTOR_FLT_EXTI_IRQn EXTI15_10_IRQn
#define LEFT_MOTOR_DIR_Pin GPIO_PIN_14
#define LEFT_MOTOR_DIR_GPIO_Port GPIOD
#define RIGHT_MOTOR_DIR_Pin GPIO_PIN_15
#define RIGHT_MOTOR_DIR_GPIO_Port GPIOD
#define LEFT_MOTOR_PWM_Pin GPIO_PIN_8
#define LEFT_MOTOR_PWM_GPIO_Port GPIOA
#define RIGHT_MOTOR_PWM_Pin GPIO_PIN_9
#define RIGHT_MOTOR_PWM_GPIO_Port GPIOA
#define STEERING_MOTOR_PWM_Pin GPIO_PIN_10
#define STEERING_MOTOR_PWM_GPIO_Port GPIOA
#define LEFT_ENCODER_A_Pin GPIO_PIN_15
#define LEFT_ENCODER_A_GPIO_Port GPIOA
#define REMOTE_UART_TX_Pin GPIO_PIN_5
#define REMOTE_UART_TX_GPIO_Port GPIOD
#define REMOTE_UART_RX_Pin GPIO_PIN_6
#define REMOTE_UART_RX_GPIO_Port GPIOD
#define LEFT_ENCODER_B_Pin GPIO_PIN_3
#define LEFT_ENCODER_B_GPIO_Port GPIOB
#define RIGHT_ENCODER_A_Pin GPIO_PIN_4
#define RIGHT_ENCODER_A_GPIO_Port GPIOB
#define RIGHT_ENCODER_B_Pin GPIO_PIN_5
#define RIGHT_ENCODER_B_GPIO_Port GPIOB
#define STEERING_ENCODER_PWM_Pin GPIO_PIN_6
#define STEERING_ENCODER_PWM_GPIO_Port GPIOB
#define LEFT_ENCODER_Z_Pin GPIO_PIN_7
#define LEFT_ENCODER_Z_GPIO_Port GPIOB
#define LEFT_ENCODER_Z_EXTI_IRQn EXTI9_5_IRQn
#define RIGHT_ENCODER_Z_Pin GPIO_PIN_9
#define RIGHT_ENCODER_Z_GPIO_Port GPIOB
#define RIGHT_ENCODER_Z_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
