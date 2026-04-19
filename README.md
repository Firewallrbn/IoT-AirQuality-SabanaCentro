# 🌍 Nodo IoT - Monitoreo de Calidad del Aire (Sabana Centro)

 

[![Estado: Completado](https://img.shields.io/badge/Estado-Completado-success.svg)](#)

[![Hardware: ESP32](https://img.shields.io/badge/Hardware-ESP32-blue.svg)](#)

[![Framework: PlatformIO](https://img.shields.io/badge/Framework-PlatformIO-orange.svg)](#)

[![Sensores: MQ135%20%7C%20HW870%20%7C%20BMP280%20%7C%20DHT22](https://img.shields.io/badge/Sensores-MQ135_%7C_HW870_%7C_BMP280_%7C_DHT22-blueviolet.svg)](#)

 

> **Prototipo de monitoreo ambiental *in situ* y de bajo costo desarrollado para la subregión de Sabana Centro, Colombia (aprox. 2.560 msnm).**

 

Este repositorio aloja el código fuente y el diseño de la arquitectura de un sistema embebido experto, capaz de medir, analizar y fusionar en tiempo real la concentración de gases contaminantes, nivel de material particulado y variables meteorológicas. El nodo ha sido diseñado para generar alertas tempranas visuales de manera autónoma y local (sin conexión a internet), aplicando los estándares de medición y semaforización de salud ocupacional **OSHA y NIOSH**.

 

<div align="center">

  <!-- REFERENCIAS A IMAGENES DEL REPOSITORIO DE CÓDIGO (NO ELIMINAR) -->

  <img src="images/nodo-frontal.jpg" alt="Vista Frontal del Nodo" width="45%" style="margin-right: 2%; border-radius: 12px; box-shadow: 0px 4px 10px rgba(0,0,0,0.1);" />

  <img src="images/nodo-lateral.png" alt="Vista Lateral del Nodo" width="45%" style="border-radius: 12px; box-shadow: 0px 4px 10px rgba(0,0,0,0.1);" />

</div>

 

<br>

 

## ✨ Características Principales

 

* **🖥️ Procesamiento Edge Autónomo:** Basado en un microcontrolador ESP32 programado de manera nativa en C++ y gestionado usando PlatformIO. Incorpora algoritmos matemáticos como la compensación de temperatura y humedad en el MQ-135.

* **🚦 Semaforización Normativa:** Lógica estricta de alertas basada en los límites de exposición permisibles de **OSHA y NIOSH**. Cuenta con una Matriz LED RGB que emite notificaciones preventivas y críticas de manera presencial.

* **📊 Visualización Local (HMI):** Interfaz implementada a través de una pantalla OLED SSD1306, que permite consultar estados, promedios e indicaciones precisas en tiempo real directamente en el dispositivo.

* **🌐 Tolerancia a Fallos Multicapa:** Protocolo de inyección de errores documentado. El dispositivo cuenta con un firmware capaz de discernir lecturas basura o desconexiones sin emitir falsas alarmas de contaminación.

 

## 🛠️ Arquitectura de Hardware y Sensores

 

La solución integra múltiples niveles de detección, asegurando que la evaluación de la calidad del aire sea completa:

 

| Componente | Función Principal |

| :--- | :--- |

| **ESP32** | Cerebro de procesamiento: bus I2C, lecturas analógicas, rutinas periódicas y matriz. |

| **MQ-135** | Detección de Gases nocivos y CO2. Calibración logarítmica ajustada dinámicamente. |

| **HW-870** | Monitoreo analógico de polvo y material particulado del entorno. |

| **BMP280** | Medición de precisión barométrica, cambios de presión y altitud (Sabana Centro). |

| **DHT22** | Muestreo en tiempo real de temperatura ambiental y humedad relativa. |

| **OLED SSD1306** | Display de interacción con el usuario final de 128x64. |

| **Matriz LED RGB**| Actuador de señalización de alertas directas y de rápida visualización a la distancia. |

 

## 📖 Documentación Rigurosa (Wiki)

 

Debido a la magnitud de diseño, calibración, justificación de impacto en la zona de Sabana Centro, esquemáticos y validación metódica, **este repositorio cuenta con una extensa documentación ubicada en su WIKI oficial**.

 

Los invitamos profundamente a explorar cada una de las páginas, donde se hace desglose nivel experto del prototipo:

 

1. **[Resumen, Motivación y Solución Propuesta]**: El encuadre del problema real en Cundinamarca y nuestro factor diferencial.

2. **[Esquemáticos y Guía de Montaje Físico]**: Explicación de circuitería, esquemáticos CAD, y control de potencia y hardware.

3. **[Análisis del Código Fuente]**: Detalles profundos sobre las librerías, lógicas de interrupción y unificación de las 3 fases del sensor.

4. **[Resultados de Experimentación y Autoevaluación]**: Gráficas de medición, análisis con inyección de errores, sensibilidad y casos de límite experimental.

5. **[Protocolos de Uso, Replicabilidad y Video de Validación]**: Instructivo detallado para escalar este proyecto a un sistema interconectado y los videos demostrativos del éxito del nodo en laboratorio.

 

👉 **[HAZ CLIC AQUÍ PARA ACCEDER A LA WIKI DEL PROYECTO ACTUALIZADA](#)** 👈  

*(Reemplaza "#" con el enlace real de la wiki de tu repositorio de Github/Gitlab).*

 

---

 

## 👨‍💻 Equipo de Desarrollo e Investigación

 

Este prototipo hardware/software fue orquestado y creado por:

 

* **Juan David Cruz Angel** (@Firewallrbn) - Liderazgo de Hardware, Arquitectura Lógica, Código y Simulación.

* **Julián David Aguilar Zambrano** - Análisis de Integración, Investigación Científica y Pruebas Iniciales.

* **Alejandro Parra Galvis** - Project Management, Soporte de Visualizaciones y Documentación Estratégica.

 

<div align="center">

  <br>

  <i>Diseñado y ejecutado para el reto de Ingeniería IoT Avanzada - 2026.</i>

</div>
